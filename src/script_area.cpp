#include "jerusalem/script_area.hpp"
#include "jerusalem/engine_api.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace jerusalem {
namespace {
constexpr std::uint32_t setup_functions[]{0x47cea0,0x47c5f0,0x46c810,0x464c30,0x460e90,0x45c030,0x459e10,0x455740,0x4513f0,0x44f510};
std::int32_t sw(std::uint32_t v) { return static_cast<std::int32_t>(v); }
double real(std::uint32_t bits) {
  float value; std::memcpy(&value,&bits,4);
  if (!std::isfinite(value)) throw ParseError("invalid setup float");
  return value;
}
std::string upper(std::string s) { for (auto& c:s) if (c>='a'&&c<='z') c=char(c-'a'+'A'); return s; }
std::string safe_name(std::string s,bool filename) {
  s=upper(s);
  for (const auto c:s) if (!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||(filename&&(c=='.'||c=='-')))) throw ParseError("unsafe setup asset name: "+s);
  return s;
}
std::string strip_suffix(std::string s) {
  const auto dot=s.rfind('.');
  if (dot!=std::string::npos) s.resize(dot);
  return s;
}
// Registration APIs: address, name, stack arguments (all thiscall).
struct SetupApi { std::uint32_t address; const char* name; unsigned args; };
constexpr SetupApi setup_apis[]{
  {0x429700,"AddObj",4},{0x429ac0,"ObjAddPuzAcc",9},{0x429be0,"ObjSetPuzAccKey",3},{0x429c20,"ObjAddRotAcc",9},
  {0x42a000,"ObjAddPre",1},{0x42a080,"ObjPreAddImgToPuz",9},{0x42a170,"ObjPreAddAniToPuz",12},
  {0x42a270,"ObjPreAddImgToRot",4},{0x42a390,"ObjPreAddAniToRot",7},{0x42a5c0,"ObjPreSetAniIdeOnPuz",4},
  {0x42a610,"ObjPreSetAniIdeOnRot",4},{0x42ad80,"AddPuz",1},{0x42af00,"PuzAddBgrImg",5},
  {0x42af90,"PuzAddMovToRot",10},{0x42b100,"PuzSetMovToRot",5},{0x42b200,"PuzAddMovToPuz",10},
  {0x42b4a0,"PuzAddAmbSou",7},{0x42b530,"AddRot",4},{0x42b860,"RotAddMovToRot",10},{0x42b9b0,"RotSetMovToRot",10},
  {0x42bab0,"RotAddMovToPuz",10},{0x42bc00,"RotSetMovToPuz",7},{0x42c040,"RotAddAmbSou",7},{0x42c0f0,"SetComBufLen",2},
  {0x42c8d0,"SouAdd",6},{0x42a7b0,"ObjPreAddTxtToPuz",13},{0x42ac20,"ObjSetPasDraCur",8},{0x42acd0,"ObjSetActDraCur",8},
};
}

std::uint32_t area_setup_function(unsigned area) {
  if (area<1 || area>10) throw ParseError("area outside 1..10");
  return setup_functions[area-1];
}

AreaModel record_area_setup(X86Machine& m,std::uint32_t app,unsigned area) {
  AreaModel r; r.area=area;
  const auto name=[&](std::uint32_t a,bool filename){ return a==0x4d0518?std::string():safe_name(m.read_string(a,64),filename); };
  // Runtime engine APIs reached during setup are recorded but have no effect.
  for (const auto& api:engine_apis)
    m.trap(api.address,[&r,&api,area](const X86Machine::Call& c)->std::uint32_t {
      ++r.calls[std::string(api.name)];
      if ((api.name=="SetSpaceSoft"||api.name=="SetSpace") && c.args.at(0)!=area) throw ParseError("setup routine selects a different area");
      if (const auto v=variable_api(api.address)) {
        if (v->kind==0) r.variables.emplace(c.args[0],AreaModel::Variable{v->size,c.args[1]});  // a repeated definition is ignored, as in aVar
        if (v->kind==1) { const auto found=r.variables.find(c.args[0]); if (found==r.variables.end()) throw ParseError("setup sets an undefined variable"); found->second.value=c.args[1]; }
        if (v->kind==2) { const auto found=r.variables.find(c.args[0]); if (found==r.variables.end()) throw ParseError("setup reads an undefined variable"); return found->second.value; }
      }
      if (api.name=="ObjPreGetX" || api.name=="ObjPreGetY") {
        unsigned index=0;
        for (const auto& l:r.stage.layers) if (l.object==c.args[0] && l.presentation==c.args[1] && index++==c.args[2]) return std::uint32_t(api.name=="ObjPreGetX"?l.x:l.y);
        throw ParseError("presentation item not registered");
      }
      if (api.name=="rand") return 0;
      if (!variable_api(api.address) && api.name!="SetSpaceSoft" && api.name!="SetSpace" && api.name!="Log" && api.name!="Log2") r.runtime_calls.push_back({api.address,c.args});
      return 1;
    },api.args,api.callee_pops);
  // sprintf (cdecl) builds names into stack buffers; it is executed natively.
  m.trap(0x490c3b,[&m](const X86Machine::Call& c)->std::uint32_t {
    const auto format=m.read_string(c.args.at(1),128);
    std::string out; unsigned next=2;
    for (std::size_t i=0;i<format.size();++i) {
      if (format[i]!='%') { out+=format[i]; continue; }
      std::string spec="%";
      while (++i<format.size() && std::string("0123456789-.").find(format[i])!=std::string::npos) spec+=format[i];
      if (i>=format.size()) throw ParseError("bad sprintf format");
      if (next>=c.args.size()) throw ParseError("too many sprintf conversions");
      const auto value=c.args[next];
      ++next;
      char buffer[96];
      if (format[i]=='s') { const auto s=m.read_string(value,64); std::snprintf(buffer,sizeof buffer,(spec+"s").c_str(),s.c_str()); }
      else if (format[i]=='d'||format[i]=='i') std::snprintf(buffer,sizeof buffer,(spec+"d").c_str(),std::int32_t(value));
      else if (format[i]=='%') { buffer[0]='%'; buffer[1]=0; --next; }
      else throw ParseError("unsupported sprintf conversion");
      out+=buffer;
    }
    if (out.size()>255) throw ParseError("sprintf result too long");
    for (std::size_t i=0;i<=out.size();++i) m.write8(c.args[0]+static_cast<std::uint32_t>(i),i<out.size()?std::uint8_t(out[i]):0);
    return static_cast<std::uint32_t>(out.size());
  },8,false);
  std::map<std::uint32_t,std::uint32_t> volumes;
  m.trap(0x42cd50,[&](const X86Machine::Call& c)->std::uint32_t { volumes[c.args[0]]=c.args[1]; ++r.calls["SouSetVol"]; return 1; },2);
  for (const auto& api:setup_apis)
    m.trap(api.address,[&,api](const X86Machine::Call& c)->std::uint32_t {
      ++r.calls[api.name];
      const auto& a=c.args;
      switch (api.address) {
        case 0x429700: r.stage.objects.push_back({a[0],m.read_string(a[1],96),name(a[2],true)}); r.object_flags[a[0]]=a[3]; break;
        case 0x42af00: r.stage.backgrounds.push_back({a[0],name(a[1],true),sw(a[2]),sw(a[3])}); break;
        case 0x429ac0: case 0x429c20: {
          const auto index=std::count_if(r.stage.access.begin(),r.stage.access.end(),[&](const StageAccess& h){return h.object==a[0];});
          const bool panorama=api.address==0x429c20;
          // A puzzle is a 640x480 screen; rectangles registered past its bottom are clipped (e.g. A02 20001).
          r.stage.access.push_back({a[0],a[1],static_cast<std::uint32_t>(index),panorama,a[6]!=0,sw(a[2]),sw(a[3]),sw(a[4]),panorama?sw(a[5]):std::min(sw(a[5]),480),a[7],a[8]});
          break;
        }
        case 0x42a270: r.stage.layers.push_back({a[0],a[1],a[2],true,false,"",a[3],1,0,1000,0,0,0}); break;
        case 0x42a390: r.stage.layers.push_back({a[0],a[1],a[2],true,true,"",a[3],a[4],a[6],1000,0,0,real(a[5])}); break;
        case 0x42a080: r.stage.layers.push_back({a[0],a[1],a[2],false,false,name(a[3],true),0,1,0,a[8],sw(a[4]),sw(a[5]),0}); break;
        case 0x42a170: r.stage.layers.push_back({a[0],a[1],a[2],false,true,name(a[3],true),a[4],a[9],a[11],a[8],sw(a[5]),sw(a[6]),real(a[10])}); break;
        case 0x42a5c0: case 0x42a610: r.animation_events.push_back({a[0],a[1],a[2],a[3],api.address==0x42a610}); break;
        case 0x42ad80: r.puzzles.push_back(a[0]); break;
        case 0x42b530: r.graph.nodes.push_back({a[0],name(a[1],false),a[3]}); break;
        case 0x42b860: case 0x42bab0: case 0x42af90: case 0x42b200: {
          const bool from_puzzle=api.address==0x42af90 || api.address==0x42b200;
          auto& exits=from_puzzle?r.puzzle_exits:r.graph.exits;
          const auto slot=std::count_if(exits.begin(),exits.end(),[&](const Exit& e){return e.source==a[0];});
          exits.push_back({a[0],static_cast<std::uint32_t>(slot),a[1],api.address==0x42bab0||api.address==0x42b200,a[7]!=0,sw(a[3]),sw(a[4]),sw(a[5]),sw(a[6]),a[8],a[9],{}});
          break;
        }
        case 0x42b9b0: case 0x42bc00: {
          const auto found=std::find_if(r.graph.exits.begin(),r.graph.exits.end(),[&](const Exit& e){return e.source==a[0] && e.slot==a[1];});
          if (found==r.graph.exits.end()) throw ParseError("rotation exit camera has no exit");
          found->arrival=api.address==0x42b9b0?Camera{real(a[7]),real(a[8]),real(a[9])}:Camera{real(a[2]),real(a[3]),real(a[4])};
          break;
        }
        case 0x42b100: {
          const auto found=std::find_if(r.puzzle_exits.begin(),r.puzzle_exits.end(),[&](const Exit& e){return e.source==a[0] && e.slot==a[1];});
          if (found==r.puzzle_exits.end()) throw ParseError("puzzle exit camera has no exit");
          found->arrival={real(a[2]),real(a[3]),real(a[4])};
          break;
        }
        case 0x42c8d0: {
          auto stem=strip_suffix(name(a[2],true));
          r.sounds[a[0]]={a[0],a[1],stem};
          if (a[1]==5) r.stage.dialogues.push_back({a[0],"A"+std::string(area<10?"0":"")+std::to_string(area),stem});
          break;
        }
        case 0x42c040: r.graph.ambient.push_back({a[0],a[1],a[2],"",100}); break;
        case 0x42b4a0: r.stage.ambient.push_back({a[0],a[1],a[2],"",100}); break;
        default: break;  // ObjAddPre, ObjSetPuzAccKey, SetComBufLen: no model data
      }
      return 1;
    },api.args);
  m.call(area_setup_function(area),app,{},20000000);
  for (auto& [id,sound]:r.sounds) if (volumes.count(id)) sound.master=volumes.at(id);
  return r;
}

void resolve_area_sounds(Bytes executable,AreaModel& r,const std::map<unsigned,AreaModel>& known) {
  const auto area=r.area;
  // Sounds are global in the original: an area may bind one registered by another.
  std::map<unsigned,AreaModel> others;
  std::map<std::uint32_t,AreaSound> system;
  for (auto* list:{&r.graph.ambient,&r.stage.ambient})
    for (auto& b:*list) {
      auto s=r.sounds.find(b.sound);
      for (unsigned other:{1U,2U,3U,4U,5U,6U,7U,8U,9U,10U}) {
        if (s!=r.sounds.end()) break;
        if (other==area) continue;
        if (!known.count(other) && !others.count(other)) {
          X86Machine fresh(executable); const auto app=fresh.scratch(0x1000); fresh.write32(0x4d0a90,app);
          others.emplace(other,record_area_setup(fresh,app,other));
        }
        const auto& sounds=known.count(other)?known.at(other).sounds:others.at(other).sounds;
        if (const auto found=sounds.find(b.sound); found!=sounds.end()) s=r.sounds.emplace(found->first,found->second).first;
      }
      if (s==r.sounds.end()) {
        // Shared interface sounds are registered by the game-wide setup (0047ffc0), which
        // also builds the interface. Read only its constant SouAdd packets:
        // push name; push category; push id; mov ecx,esi; call SouAdd.
        if (system.empty()) {
          X86Machine image(executable);
          for (std::uint32_t a=0x47ffc0;a<0x483b22;++a) {
            if (image.read8(a)!=0x68 || image.read8(a+5)!=0x6a || image.read8(a+7)!=0x68 || image.read8(a+12)!=0x8b || image.read8(a+13)!=0xce || image.read8(a+14)!=0xe8) continue;
            if (a+19+image.read32(a+15)!=0x42c8d0) continue;
            const auto id=image.read32(a+8);
            system.emplace(id,AreaSound{id,image.read8(a+6),strip_suffix(safe_name(image.read_string(image.read32(a+1),64),true))});
          }
        }
        if (const auto found=system.find(b.sound); found!=system.end()) s=r.sounds.emplace(found->first,found->second).first;
      }
      if (s==r.sounds.end()) throw ParseError("ambient binding without sound "+std::to_string(b.sound));
      b.stem=s->second.stem;
      b.master=s->second.master;
    }
  }

AreaModel extract_area_model(Bytes executable,unsigned area) {
  X86Machine m(executable);
  const auto app=m.scratch(0x1000);
  m.write32(0x4d0a90,app);
  auto r=record_area_setup(m,app,area);
  resolve_area_sounds(executable,r,{});
  return r;
}
}  // namespace jerusalem
