#include "jerusalem/navigation.hpp"
#include "jerusalem/interactions.hpp"
#include "jerusalem/stage.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace jerusalem {
namespace {
constexpr std::uint32_t begin=0x47cea0,end=0x47d8de;
class Pe {
  Bytes b_;
  struct Section { std::uint32_t rva,virtual_size,size,offset; };
  std::vector<Section> sections_;
 public:
  explicit Pe(Bytes b):b_(b) {
    if (!b.is(0,"MZ")) throw ParseError("not a PE executable");
    const auto pe=std::size_t(b.u32(0x3c));
    b.require(pe,24);
    if (!b.is(pe,std::string("PE\0\0",4)) || b.u16(pe+4)!=0x14c) throw ParseError("require x86 PE32");
    const auto count=b.u16(pe+6), optional=b.u16(pe+20);
    if (!count || count>96 || optional<96) throw ParseError("invalid PE section table");
    b.require(pe+24,optional);
    if (b.u16(pe+24)!=0x10b || b.u32(pe+52)!=0x400000) throw ParseError("unsupported PE image base");
    const auto table=pe+24+optional;
    for (unsigned i=0;i<count;++i) {
      const auto p=table+i*40; b.require(p,40);
      Section s{b.u32(p+12),b.u32(p+8),b.u32(p+16),b.u32(p+20)};
      b.require(s.offset,s.size); sections_.push_back(s);
    }
  }
  Bytes at(std::uint32_t address,std::size_t size) const {
    if (address<0x400000) throw ParseError("PE address outside image");
    const auto rva=address-0x400000;
    for (const auto& s:sections_) if (rva>=s.rva && rva-s.rva<s.size) {
      const auto delta=rva-s.rva;
      if (size>s.size-delta) throw ParseError("PE address spans section bounds");
      return b_.slice(std::size_t(s.offset)+delta,size);
    }
    throw ParseError("PE address is not file backed");
  }
  std::string label(std::uint32_t address) const {
    std::string result;
    for(unsigned i=0;i<96;++i) {const auto c=at(address+i,1).u8(0);if(!c)return result;if(c<32 || c>126)throw ParseError("unsupported item label encoding");result+=char(c);}
    throw ParseError("unterminated item label");
  }
  std::string name(std::uint32_t address,bool filename=false) const {
    std::string text;
    for (unsigned i=0;i<64;++i) {
      auto c=at(address+i,1).u8(0);
      if (filename && c>='a' && c<='z') c=std::uint8_t(c-'a'+'A');
      if (!c) return text;
      if (!((c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || (filename && c=='.'))) throw ParseError("unsafe scene asset name");
      text+=char(c);
    }
    throw ParseError("unterminated scene asset name");
  }
};
double real(std::uint32_t bits) {
  float value; std::memcpy(&value,&bits,4);
  if (!std::isfinite(value)) throw ParseError("invalid scene camera float");
  return value;
}
std::int32_t signed_word(std::uint32_t value) {
  return value<=0x7fffffffU ? static_cast<std::int32_t>(value) :
      static_cast<std::int32_t>(static_cast<std::int64_t>(value)-0x100000000LL);
}
}

std::vector<SetupCall> parse_setup_calls(Bytes b,std::uint32_t address) {
  if (b.size()>65536 || std::uint64_t(address)+b.size()>0x100000000ULL ||
      !b.is(0,std::string("\x56\x8b\xf1",3))) throw ParseError("unsupported setup prologue");
  std::size_t p=3;
  std::vector<std::uint32_t> stack;
  std::vector<SetupCall> calls;
  while (p<b.size()) {
    const auto op=b.u8(p);
    if (op==0x6a) {
      const auto v=b.u8(p+1);
      stack.push_back(v<128 ? v : 0xffffff00U|v); p+=2;
    } else if (op==0x68) { stack.push_back(b.u32(p+1)); p+=5; }
    else if (b.is(p,std::string("\x8b\xce",2))) {
      p+=2; if (b.u8(p)!=0xe8) throw ParseError("unexpected instruction after setup receiver");
    } else if (op==0xe8) {
      const auto target=std::int64_t(address)+static_cast<std::int64_t>(p)+5+b.i32(p+1);
      if (target<0 || target>0xffffffffLL || calls.size()>=256 || stack.empty()) throw ParseError("invalid setup call");
      std::reverse(stack.begin(),stack.end());
      calls.push_back({address+static_cast<std::uint32_t>(p),static_cast<std::uint32_t>(target),std::move(stack)});
      stack.clear(); p+=5;
    } else throw ParseError("unsupported setup instruction at byte "+std::to_string(p));
    if (stack.size()>16) throw ParseError("too many setup arguments");
  }
  if (!stack.empty()) throw ParseError("unfinished setup argument packet");
  return calls;
}

StageSetup extract_a01_stage(Bytes b) {
  // Reuse the version gate; registration bytes are interpreted as constants, never executed.
  (void)extract_a01_navigation(b);
  const Pe pe(b);StageSetup result;
  const auto consume=[&](const std::vector<SetupCall>& calls) {
    for(const auto& call:calls) {
      const auto& a=call.arguments;
      if(call.target==0x42af00) {
        if(a.size()!=5 || a[4]!=1)throw ParseError("unsupported puzzle background");
        result.backgrounds.push_back({a[0],pe.name(a[1],true),signed_word(a[2]),signed_word(a[3])});
      }else if(call.target==0x429ac0 || call.target==0x429c20) {
        if(a.size()!=9 || a[6]>1)throw ParseError("unsupported story access");
        const auto index=std::count_if(result.access.begin(),result.access.end(),[&](const StageAccess& h){return h.object==a[0];});
        result.access.push_back({a[0],a[1],static_cast<std::uint32_t>(index),call.target==0x429c20,a[6]!=0,signed_word(a[2]),signed_word(a[3]),signed_word(a[4]),signed_word(a[5]),a[7],a[8]});
      }else if(call.target==0x42a270 || call.target==0x42a390) {
        const bool animated=call.target==0x42a390;
        if(a.size()!=(animated?7U:4U))throw ParseError("unsupported story rotation layer");
        result.layers.push_back({a[0],a[1],a[2],true,animated,"",a[3],animated?a[4]:1,animated?a[6]:0,1000,0,0,animated?real(a[5]):0});
      }else if(call.target==0x42a080 || call.target==0x42a170) {
        const bool animated=call.target==0x42a170;
        if(a.size()!=(animated?12U:9U))throw ParseError("unsupported story puzzle layer");
        if(animated && a[4]!=0 && a[4]!=1)throw ParseError("unsupported animation resource selector");
        result.layers.push_back({a[0],a[1],a[2],false,animated,pe.name(a[3],true),animated?a[4]:0,animated?a[9]:1,animated?a[11]:0,a[8],signed_word(a[animated?5:4]),signed_word(a[animated?6:5]),animated?real(a[10]):0});
        // ObjPreAddAniToPuz: object,pres,puzzle,stem,type,x,y,flags,priority,count,fps,mode.
      }
    }
  };
  consume(parse_setup_calls(pe.at(begin,end-begin),begin));
  for(const auto& range:{std::pair{end,0x47e450U},std::pair{0x47e450U,0x47f01bU}}) {
    const auto bytes=pe.at(range.first,range.second-range.first);std::vector<std::uint8_t> code{0x56,0x8b,0xf1};code.insert(code.end(),bytes.data(),bytes.data()+bytes.size());
    consume(parse_setup_calls(Bytes(code),range.first-3));
  }
  // The sound setup uses runtime load-source arguments. Read only the pinned ID/name pushes.
  for(std::uint32_t address=0x47fab0;address<0x47ff60;++address) {
    const auto p=pe.at(address,19);
    if(p.u8(0)==0x68 && p.u8(5)==0x6a && p.u8(6)==5 && p.u8(7)==0x68 && p.is(12,std::string("\x8b\xce\xe8",3)) && std::int64_t(address)+19+p.i32(15)==0x42c8d0) {
      auto name=pe.name(p.u32(1),true);if(name.size()<5 || name.substr(name.size()-4)!=".WAC")throw ParseError("unsupported story voice suffix");name.resize(name.size()-4);
      result.dialogues.push_back({p.u32(8),"A01",std::move(name)});
    }
  }
  // Round-trip validates every field before exposing a usable model.
  return parse_stage(write_stage(result));
}

SceneGraph extract_a01_navigation(Bytes b) {
  if (b.size()!=811008) throw ParseError("unsupported Jerusalem executable size/version");
  std::uint64_t fingerprint=14695981039346656037ULL;
  for (std::size_t i=0;i<b.size();++i) fingerprint=(fingerprint^b.u8(i))*1099511628211ULL;
  if (fingerprint!=0x64b12e05ac088220ULL) throw ParseError("unsupported Jerusalem executable fingerprint");
  const Pe pe(b);
  const auto calls=parse_setup_calls(pe.at(begin,end-begin),begin);
  SceneGraph g;
  std::set<std::pair<std::uint32_t,std::uint32_t>> configured;
  for (const auto& call:calls) {
    const auto& a=call.arguments;
    if (call.target==0x42b530) {
      if (a.size()!=4 || a[2]!=0) throw ParseError("unsupported node registration");
      g.nodes.push_back({a[0],pe.name(a[1]),a[3]});
    } else if (call.target==0x42b860 || call.target==0x42bab0) {
      if (a.size()!=10 || a[2]!=0x4d0518 || a[7]>1) throw ParseError("unsupported exit registration");
      const auto slot=std::count_if(g.exits.begin(),g.exits.end(),[&](const Exit& e){return e.source==a[0];});
      g.exits.push_back({a[0],static_cast<std::uint32_t>(slot),a[1],call.target==0x42bab0,a[7]!=0,
                        signed_word(a[3]),signed_word(a[4]),signed_word(a[5]),signed_word(a[6]),a[8],a[9],{}});
    } else if (call.target==0x42b9b0 || call.target==0x42bc00) {
      if (a.size()!=(call.target==0x42b9b0 ? 10U : 7U)) throw ParseError("unsupported exit camera packet");
      const auto found=std::find_if(g.exits.begin(),g.exits.end(),[&](const Exit& e){return e.source==a[0] && e.slot==a[1];});
      if (found==g.exits.end() || !configured.insert({a[0],a[1]}).second) throw ParseError("exit camera has no unique owner");
      if (call.target==0x42b9b0) {
        if (found->puzzle || a[5]!=0 || a[6]!=1 || a[2]!=a[7] || a[3]!=a[8] || a[4]!=a[9])
          throw ParseError("unsupported transition camera semantics");
        found->arrival={real(a[7]),real(a[8]),real(a[9])};
      } else {
        if (!found->puzzle || a[5]!=0 || a[6]!=2) throw ParseError("unsupported puzzle camera semantics");
        found->arrival={real(a[2]),real(a[3]),real(a[4])};
      }
    }
  }
  if (g.nodes.size()!=6 || g.exits.size()!=17 || configured.size()!=17 ||
      std::count_if(g.exits.begin(),g.exits.end(),[](const Exit& e){return e.puzzle;})!=1)
    throw ParseError("incomplete supported A01 scene setup");
  // Three verified constant registration ranges; no game instructions execute.
  const auto packet=[&](std::uint32_t start,std::uint32_t stop) {
    const auto bytes=pe.at(start,stop-start);
    std::vector<std::uint8_t> code{0x56,0x8b,0xf1};
    code.insert(code.end(),bytes.data(),bytes.data()+bytes.size());
    return parse_setup_calls(Bytes(code),start-3);
  };
  // Read the ID/name pushes from the actual SouAdd packets. Runtime load-source
  // configuration preceding these pushes is deliberately outside this extractor.
  std::map<std::uint32_t,std::string> sounds;
  for (const auto address:{0x47f1feU,0x47f4f7U,0x47fa01U}) {
    const auto p=pe.at(address,19);
    if (p.u8(0)!=0x68 || p.u8(5)!=0x6a || p.u8(6)!=2 || p.u8(7)!=0x68 ||
        !p.is(12,std::string("\x8b\xce\xe8",3)) ||
        std::int64_t(address)+19+p.i32(15)!=0x42c8d0)
      throw ParseError("unsupported ambient sound registration");
    auto name=pe.name(p.u32(1),true);
    if (name.size()<5 || (name.substr(name.size()-4)!=".WAV" && name.substr(name.size()-4)!=".WAS"))
      throw ParseError("unsupported ambient resource suffix");
    name.resize(name.size()-4);
    sounds.emplace(p.u32(8),std::move(name));
  }
  for (const auto& range:{std::pair{0x47f21fU,0x47f2c1U},std::pair{0x47f518U,0x47f5baU},std::pair{0x47fa22U,0x47faa9U}})
    for (const auto& call:packet(range.first,range.second)) {
      const auto& a=call.arguments;
      if (call.target!=0x42c040 || a.size()!=7 || a[3]!=0 || a[4]!=1 || a[5]!=1 || a[6]!=10 || !sounds.count(a[1]))
        throw ParseError("unsupported ambient scene binding");
      g.ambient.push_back({a[0],a[1],a[2],sounds.at(a[1])});
    }
  if (g.ambient.size()!=17) throw ParseError("incomplete A01 ambient setup");
  std::map<std::uint32_t,PickupEffect> effects;
  for(const auto address:{0x44a74cU,0x44a7b1U,0x44a816U}) {
    const auto effect=parse_pickup_effect(pe.at(address,79),address);effects.emplace(effect.object,effect);
  }
  std::map<std::uint32_t,Collectible> objects;
  std::set<std::uint32_t> hotspots;
  for(const auto& call:packet(0x47e493,0x47e62b)) {
    const auto& a=call.arguments;
    if(a.empty() || !effects.count(a[0])) continue;
    if(call.target==0x429700) {
      if(a.size()!=4 || a[3]!=1 || objects.count(a[0])) throw ParseError("unsupported collectible declaration");
      Collectible object{};object.id=a[0];object.name=pe.label(a[1]);
      object.counter=effects.at(a[0]).counter;object.milestone=effects.at(a[0]).milestone;
      objects.emplace(object.id,std::move(object));
    } else if(call.target==0x429c20) {
      if(a.size()!=9 || a[6]!=1 || a[7]!=60 || a[8]!=0 || !objects.count(a[0]) || !hotspots.insert(a[0]).second)
        throw ParseError("unsupported collectible hotspot");
      auto& item=objects.at(a[0]);item.node=a[1];item.left=signed_word(a[2]);item.top=signed_word(a[3]);
      item.right=signed_word(a[4]);item.bottom=signed_word(a[5]);item.cursor=a[7];item.event=a[8];
    } else if(call.target==0x42a270) {
      if(a.size()!=4 || a[1]!=0 || !objects.count(a[0])) throw ParseError("unsupported collectible channel binding");
      objects.at(a[0]).channels.push_back({a[2],a[3]});
    } else if(call.target!=0x42a000 || a.size()!=1) throw ParseError("unknown collectible setup operation");
  }
  std::map<std::uint32_t,std::uint32_t> initial;
  for(const auto& call:packet(0x44f0ff,0x44f145)) {
    if(call.target!=0x42c1a0 || call.arguments.size()!=2) throw ParseError("unsupported initial pickup variables");
    initial.emplace(call.arguments[0],call.arguments[1]);
  }
  for(const auto& call:packet(0x44f06b,0x44f095)) {
    const auto& a=call.arguments;
    if(call.target!=0x42a920 || a.size()!=2 || a[1]!=0 || !objects.count(a[0])) throw ParseError("unsupported initial collectible visibility");
    const auto& item=objects.at(a[0]);
    if(!initial.count(item.counter) || initial.at(item.counter)!=0 || !hotspots.count(item.id)) throw ParseError("unproven fresh collectible state");
    g.items.push_back(item);
  }
  if(g.items.size()!=3) throw ParseError("incomplete supported collectible setup");
  // The fingerprint above pins the dispatch and its symmetric argument checks.
  const auto combination_calls=packet(0x44b6b8,0x44b6dc);
  if(combination_calls.size()!=3 || combination_calls[0].target!=0x42c460 || combination_calls[1].target!=0x42c460 || combination_calls[2].target!=0x42c3a0 ||
     combination_calls[0].arguments.size()!=1 || combination_calls[1].arguments.size()!=1 || combination_calls[2].arguments.size()!=1) throw ParseError("unsupported inventory combination effects");
  Combination combination{combination_calls[0].arguments[0],combination_calls[1].arguments[0],combination_calls[2].arguments[0],""};
  for(const auto& call:packet(0x47e5af,0x47e5c7)) if(call.target==0x429700 && call.arguments.size()==4 && call.arguments[0]==combination.result && call.arguments[3]==1) combination.name=pe.label(call.arguments[1]);
  g.combinations.push_back(std::move(combination));
  const auto palm_effect=packet(0x44ab55,0x44ab89);
  if(palm_effect.size()!=4 || palm_effect[0].target!=0x42c1a0 || palm_effect[0].arguments.size()!=2 || palm_effect[1].target!=0x42c3a0 || palm_effect[1].arguments.size()!=1 ||
     palm_effect[2].target!=0x42a9a0 || palm_effect[3].target!=0x429d70)throw ParseError("unsupported assignment pickup");
  Collectible palm{};palm.id=palm_effect[1].arguments[0];palm.counter=palm_effect[0].arguments[0];palm.value=palm_effect[0].arguments[1];palm.assign=true;
  for(const auto& call:packet(0x47e99f,0x47ea18)) {
    const auto& a=call.arguments;if(a.empty() || a[0]!=palm.id)throw ParseError("inconsistent palm registration");
    if(call.target==0x429700 && a.size()==4)palm.name=pe.label(a[1]);
    else if(call.target==0x429c20 && a.size()==9 && a[6]==1){palm.node=a[1];palm.left=signed_word(a[2]);palm.top=signed_word(a[3]);palm.right=signed_word(a[4]);palm.bottom=signed_word(a[5]);palm.cursor=a[7];palm.event=a[8];}
    else if(call.target==0x42a270 && a.size()==4 && a[1]==0)palm.channels.push_back({a[2],a[3]});
    else if(call.target!=0x42a000 || a.size()!=1)throw ParseError("unsupported palm setup");
  }
  const auto fresh_palm=packet(0x44f095,0x44f0a3);
  if(fresh_palm.size()!=1 || fresh_palm[0].target!=0x42a920 || fresh_palm[0].arguments!=std::vector<std::uint32_t>{palm.id,0} || initial.at(palm.counter)!=0)throw ParseError("unproven initial palm state");
  if(palm_effect[2].arguments!=std::vector<std::uint32_t>{palm.id,0} || palm_effect[3].arguments!=std::vector<std::uint32_t>{palm.id})throw ParseError("inconsistent palm effect targets");
  g.items.push_back(palm);
  for(const auto address:{0x44ab99U,0x44abc5U,0x44abf5U}) {
    const auto calls=packet(address,address+28);
    if(calls.size()!=2 || calls[0].target!=0x42a9a0 || calls[1].target!=0x42a920 || calls[0].arguments.size()!=2 || calls[1].arguments.size()!=2 || calls[0].arguments[0]!=calls[1].arguments[0])throw ParseError("unsupported pickup presentation switch");
    g.pickup_switches.push_back({palm.id,pe.at(0x44ab8a,4).u32(0),static_cast<std::uint32_t>(g.pickup_switches.size()+1),calls[0].arguments[0],calls[0].arguments[1],calls[1].arguments[1]});
  }
  std::map<std::pair<std::uint32_t,std::uint32_t>,Presentation> presentations;
  for(const auto& call:packet(0x47e6d2,0x47e99f))if(call.target==0x42a270) {
    const auto& a=call.arguments;if(a.size()!=4)throw ParseError("unsupported character channel");
    auto& presentation=presentations[{a[0],a[1]}];presentation.object=a[0];presentation.index=a[1];presentation.channels.push_back({a[2],a[3]});
  }
  for(auto& entry:presentations)g.presentations.push_back(std::move(entry.second));
  for(const auto& range:{std::pair{0x47e450U,0x47e493U},std::pair{0x47ea18U,0x47ea5bU},std::pair{0x47e6d2U,0x47e715U}})
    for(const auto& call:packet(range.first,range.second))if(call.target==0x429c20) {
      const auto& a=call.arguments;if(a.size()!=9 || a[6]>1)throw ParseError("unsupported item-use target");
      g.targets.push_back({a[0],a[1],signed_word(a[2]),signed_word(a[3]),signed_word(a[4]),signed_word(a[5]),a[7],a[8],a[6]!=0,"Character hotspot "+std::to_string(a[0])});
    }
  if(g.targets.size()!=3)throw ParseError("incomplete interaction target setup");
  const auto offer=packet(0x44a886,0x44a8a0),enable=packet(0x44a8be,0x44a8ca);
  if(offer.size()!=2 || offer[0].target!=0x42c1a0 || offer[0].arguments.size()!=2 || offer[1].target!=0x42c460 || offer[1].arguments.size()!=1 || enable.size()!=1 || enable[0].target!=0x429d50 || enable[0].arguments.size()!=1)throw ParseError("unsupported matting offer");
  g.uses.push_back({1,g.targets[1].id,offer[1].arguments[0],offer[0].arguments[0],offer[0].arguments[1],enable[0].arguments[0],pe.at(0x44a8a1,4).u32(0),pe.at(0x44a8b5,4).u32(0),1,0,0,true});
  // Read the dialogue registration's constant suffix; dynamic load configuration is not interpreted.
  const auto speech=pe.at(0x47fab3,19);
  if(speech.u8(0)!=0x68 || speech.u8(5)!=0x6a || speech.u8(6)!=5 || speech.u8(7)!=0x68 || !speech.is(12,std::string("\x8b\xce\xe8",3)) || std::int64_t(0x47fab3)+19+speech.i32(15)!=0x42c8d0)throw ParseError("unsupported speech registration");
  auto stem=pe.name(speech.u32(1),true);if(stem.size()<5 || stem.substr(stem.size()-4)!=".WAC")throw ParseError("unsupported dialogue sound suffix");stem.resize(stem.size()-4);
  g.dialogues.push_back({speech.u32(8),g.nodes.front().asset.substr(0,3),stem});
  const auto response=packet(0x44a690,0x44a69e);
  if(response.size()!=1 || response[0].target!=0x42ccd0 || response[0].arguments!=std::vector<std::uint32_t>{g.dialogues.front().id,1})throw ParseError("unsupported item response");
  for(const auto item:{g.combinations.front().first,g.combinations.front().second,g.combinations.front().result})g.uses.push_back({static_cast<std::uint32_t>(g.uses.size()+1),g.targets.front().id,item,0,0,0,0,0,0,0,g.dialogues.front().id,false});
  for(const auto& r:g.pickup_switches)if(initial.at(r.variable)!=0)throw ParseError("unproven initial character state");
  const auto hidden=packet(0x44f0db,0x44f0ff);if(hidden.size()!=3)throw ParseError("incomplete initial hidden character setup");
  for(const auto& call:hidden)if(call.target!=0x42ab20 || call.arguments.size()!=1 || std::none_of(g.presentations.begin(),g.presentations.end(),[&](const Presentation& p){return p.object==call.arguments[0];}))throw ParseError("unsupported initial character visibility");
  std::sort(g.targets.begin(),g.targets.end(),[](const InteractionTarget& a,const InteractionTarget& b){return a.id<b.id;});
  validate_navigation(g);
  return g;
}

const SceneNode& scene_node(const SceneGraph& g,std::uint32_t id) {
  const auto node=std::find_if(g.nodes.begin(),g.nodes.end(),[&](const SceneNode& n){return n.id==id;});
  if (node==g.nodes.end()) throw ParseError("scene ID does not exist");
  return *node;
}
bool has_story_rules(const SceneGraph& g) {return !g.targets.empty() || !g.presentations.empty() || !g.pickup_switches.empty() || !g.uses.empty() || !g.dialogues.empty() || std::any_of(g.items.begin(),g.items.end(),[](const Collectible& c){return c.assign;});}
SceneGraph legacy_navigation(SceneGraph g,unsigned revision) {
  if(revision<3){g.items.erase(std::remove_if(g.items.begin(),g.items.end(),[](const Collectible& c){return c.assign;}),g.items.end());g.targets.clear();g.presentations.clear();g.pickup_switches.clear();g.uses.clear();g.dialogues.clear();}
  if(revision<2)g.combinations.clear();
  return g;
}
void validate_navigation(const SceneGraph& g) {
  if (g.nodes.empty() || g.nodes.size()>64 || g.exits.size()>512 || g.ambient.size()>192 || g.items.size()>16) throw ParseError("invalid navigation record counts");
  std::set<std::uint32_t> ids;
  std::set<std::string> names;
  std::set<std::pair<std::uint32_t,std::uint32_t>> slots;
  for (const auto& n:g.nodes) {
    if (!n.id || n.id>0x7fffffffU || !ids.insert(n.id).second || !names.insert(n.asset).second || n.asset.empty() || n.asset.size()>63 || n.channels>256)
      throw ParseError("invalid/duplicate scene node");
    for (unsigned char c:n.asset) if (!((c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_'))
      throw ParseError("unsafe scene asset name");
  }
  std::set<std::pair<std::uint32_t,std::uint32_t>> bindings;
  std::map<std::uint32_t,std::string> sound_names;
  std::map<std::string,std::uint32_t> sound_ids;
  for (const auto& a:g.ambient) {
    scene_node(g,a.node);
    if (!a.sound || a.sound>0x7fffffffU || a.volume>100 || a.stem.empty() || a.stem.size()>63 ||
        !bindings.insert({a.node,a.sound}).second) throw ParseError("invalid/duplicate ambient binding");
    for (unsigned char c:a.stem) if (!((c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_'))
      throw ParseError("unsafe ambient resource stem");
    if ((sound_names.count(a.sound) && sound_names.at(a.sound)!=a.stem) ||
        (sound_ids.count(a.stem) && sound_ids.at(a.stem)!=a.sound)) throw ParseError("conflicting ambient identity");
    sound_names[a.sound]=a.stem; sound_ids[a.stem]=a.sound;
    if (std::count_if(g.ambient.begin(),g.ambient.end(),[&](const SceneAmbient& other){return other.node==a.node;})>3)
      throw ParseError("too many ambient voices in scene");
  }
  std::set<std::uint32_t> item_ids;
  for(const auto& item:g.items) {
    scene_node(g,item.node);
    if(!item.id || item.id>0x7fffffffU || !item_ids.insert(item.id).second || !item.counter || item.counter>0x7fffffffU ||
       item.milestone>127 || item.cursor>0x7fffffffU || item.event!=0 || item.name.empty() || item.name.size()>95 ||
       item.channels.empty() || item.channels.size()>64 || item.left<0 || item.left>=item.right || item.right>3600 ||
       item.top< -900 || item.top>=item.bottom || item.bottom>900) throw ParseError("invalid collectible record");
    for(unsigned char c:item.name) if(c<32 || c>126) throw ParseError("unsupported item label encoding");
    std::set<std::pair<std::uint32_t,std::uint32_t>> bound;
    for(const auto& b:item.channels) if(b.channel>=scene_node(g,b.node).channels || !bound.insert({b.node,b.channel}).second)
      throw ParseError("invalid/duplicate collectible channel binding");
  }
  if(g.combinations.size()>16)throw ParseError("too many inventory combinations");
  std::set<std::uint32_t> results;
  std::set<std::pair<std::uint32_t,std::uint32_t>> pairs;
  for(const auto& c:g.combinations) {
    // This milestone supports consuming pairs of original pickups, without chaining.
    if(!item_ids.count(c.first) || !item_ids.count(c.second) || c.first==c.second || !c.result || c.result>0x7fffffffU || item_ids.count(c.result) ||
       !results.insert(c.result).second || !pairs.insert(std::minmax(c.first,c.second)).second || c.name.empty() || c.name.size()>95)throw ParseError("invalid inventory combination");
    for(unsigned char ch:c.name)if(ch<32 || ch>126)throw ParseError("unsupported combination label encoding");
  }
  const auto printable=[](const std::string& text){return !text.empty() && text.size()<96 && std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32 && c<=126;});};
  if(g.targets.size()>32 || g.presentations.size()>32 || g.pickup_switches.size()>32 || g.uses.size()>32 || g.dialogues.size()>32)throw ParseError("too many interaction rules");
  std::set<std::pair<std::uint32_t,std::uint32_t>> presentation_ids;
  for(const auto& p:g.presentations){if(!p.object || p.index>15 || p.channels.empty() || p.channels.size()>64 || !presentation_ids.insert({p.object,p.index}).second)throw ParseError("invalid static presentation");std::set<std::pair<std::uint32_t,std::uint32_t>> seen;for(const auto& b:p.channels)if(b.channel>=scene_node(g,b.node).channels || !seen.insert({b.node,b.channel}).second)throw ParseError("invalid presentation binding");}
  std::set<std::uint32_t> targets,dialogues,uses;
  for(const auto& t:g.targets){scene_node(g,t.node);if(!t.id || t.id>0x3fffffffU || item_ids.count(t.id) || !targets.insert(t.id).second || t.event!=0 || t.cursor>0x7fffffffU || !printable(t.name) || t.left<0 || t.left>=t.right || t.right>3600 || t.top< -900 || t.top>=t.bottom || t.bottom>900)throw ParseError("invalid interaction target");}
  for(const auto& d:g.dialogues){if(!d.id || !dialogues.insert(d.id).second || d.area.size()!=3 || d.area[0]!='A' || d.area[1]<'0' || d.area[1]>'9' || d.area[2]<'0' || d.area[2]>'9' || d.stem.empty() || d.stem.size()>63)throw ParseError("invalid dialogue resource");for(unsigned char c:d.stem)if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))throw ParseError("unsafe dialogue stem");}
  std::set<std::pair<std::uint32_t,std::uint32_t>> switch_keys;
  for(const auto& r:g.pickup_switches)if(!item_ids.count(r.item) || !r.variable || r.variable>0x3fffffffU || r.value>255 || !switch_keys.insert({r.item,r.value}).second || !presentation_ids.count({r.object,r.hide}) || !presentation_ids.count({r.object,r.show}))throw ParseError("invalid pickup presentation switch");
  std::set<std::pair<std::uint32_t,std::uint32_t>> use_pairs;
  for(const auto& r:g.uses){
    if(!r.id || r.id>0x3fffffffU || !uses.insert(r.id).second || !targets.count(r.target) || (!item_ids.count(r.item) && !results.count(r.item)) || !use_pairs.insert({r.target,r.item}).second || r.value>255 || r.variable>0x3fffffffU || r.choice>0x3fffffffU || (r.enable && !targets.count(r.enable)) || (r.dialogue && !dialogues.count(r.dialogue)))throw ParseError("invalid item-use rule");
    if(r.consume){if(!r.variable || !r.object || !r.choice || r.dialogue || !item_ids.count(r.item) || !presentation_ids.count({r.object,r.zero}) || !presentation_ids.count({r.object,r.nonzero}))throw ParseError("unsupported consuming use effect");}
    else if(!r.dialogue || r.variable || r.value || r.object || r.choice || r.enable || r.zero || r.nonzero)throw ParseError("unsupported response-only use effect");
  }
  if(has_story_rules(g)){for(const auto& item:g.items)if(item.id>0x3fffffffU || (item.assign && (item.value>255 || item.milestone)) || (!item.assign && item.value))throw ParseError("invalid assignment pickup");for(const auto& c:g.combinations)if(c.result>0x3fffffffU)throw ParseError("combination result exceeds action ID range");}
  for (const auto& e:g.exits) {
    scene_node(g,e.source); if (!e.puzzle) scene_node(g,e.target);
    if (!e.target || e.target>0x7fffffffU || e.cursor>0x7fffffffU || e.event>0x7fffffffU ||
        e.slot>=64 || !slots.insert({e.source,e.slot}).second || e.left<0 || e.left>=e.right || e.right>3600 ||
        e.top< -900 || e.top>=e.bottom || e.bottom>900 ||
        !std::isfinite(e.arrival.yaw) || e.arrival.yaw<0 || e.arrival.yaw>360 ||
        !std::isfinite(e.arrival.pitch) || std::abs(e.arrival.pitch)>90 ||
        !std::isfinite(e.arrival.fov) || e.arrival.fov<30 || e.arrival.fov>87)
      throw ParseError("invalid navigation exit");
  }
}

std::string write_navigation(const SceneGraph& g) {
  validate_navigation(g);
  std::ostringstream out; out.imbue(std::locale::classic()); out << (has_story_rules(g)?"JERUSALEM_A01_NAV_V5\n":!g.combinations.empty()?"JERUSALEM_A01_NAV_V4\n":!g.items.empty()?"JERUSALEM_A01_NAV_V3\n":g.ambient.empty()?"JERUSALEM_A01_NAV_V1\n":"JERUSALEM_A01_NAV_V2\n") << std::setprecision(12);
  for (const auto& n:g.nodes) out << "node " << n.id << ' ' << n.asset << ' ' << n.channels << '\n';
  for (const auto& e:g.exits)
    out << "exit " << e.source << ' ' << e.slot << ' ' << e.target << ' ' << e.puzzle << ' ' << e.enabled << ' '
        << e.left << ' ' << e.top << ' ' << e.right << ' ' << e.bottom << ' ' << e.cursor << ' ' << e.event << ' '
        << e.arrival.yaw << ' ' << e.arrival.pitch << ' ' << e.arrival.fov << '\n';
  for (const auto& a:g.ambient) out << "ambient " << a.node << ' ' << a.sound << ' ' << a.volume << ' ' << a.stem << '\n';
  for(const auto& item:g.items) {
    out << "item " << item.id << ' ' << item.node << ' ' << item.left << ' ' << item.top << ' ' << item.right << ' ' << item.bottom << ' '
        << item.cursor << ' ' << item.event << ' ' << item.counter << ' ' << item.milestone << ' ' << std::quoted(item.name) << '\n';
    for(const auto& b:item.channels) out << "item-channel " << item.id << ' ' << b.node << ' ' << b.channel << '\n';
  }
  for(const auto& c:g.combinations)out << "combine " << c.first << ' ' << c.second << ' ' << c.result << ' ' << std::quoted(c.name) << '\n';
  for(const auto& item:g.items)if(item.assign)out << "pickup-set " << item.id << ' ' << item.value << '\n';
  for(const auto& p:g.presentations)for(const auto& b:p.channels)out << "presentation " << p.object << ' ' << p.index << ' ' << b.node << ' ' << b.channel << '\n';
  for(const auto& r:g.pickup_switches)out << "pickup-switch " << r.item << ' ' << r.variable << ' ' << r.value << ' ' << r.object << ' ' << r.hide << ' ' << r.show << '\n';
  for(const auto& t:g.targets)out << "target " << t.id << ' ' << t.node << ' ' << t.left << ' ' << t.top << ' ' << t.right << ' ' << t.bottom << ' ' << t.cursor << ' ' << t.event << ' ' << t.enabled << ' ' << std::quoted(t.name) << '\n';
  for(const auto& r:g.uses)out << "use " << r.id << ' ' << r.target << ' ' << r.item << ' ' << r.variable << ' ' << r.value << ' ' << r.enable << ' ' << r.choice << ' ' << r.object << ' ' << r.zero << ' ' << r.nonzero << ' ' << r.dialogue << ' ' << r.consume << '\n';
  for(const auto& d:g.dialogues)out << "dialogue " << d.id << ' ' << d.area << ' ' << d.stem << '\n';
  return out.str();
}
SceneGraph parse_navigation(const std::string& text) {
  if (text.size()>128*1024) throw ParseError("navigation model exceeds 128 KiB limit");
  std::istringstream lines(text); std::string line;
  if (!std::getline(lines,line) || (line!="JERUSALEM_A01_NAV_V1" && line!="JERUSALEM_A01_NAV_V2" && line!="JERUSALEM_A01_NAV_V3" && line!="JERUSALEM_A01_NAV_V4" && line!="JERUSALEM_A01_NAV_V5")) throw ParseError("not a native A01 navigation model");
  const bool story=line=="JERUSALEM_A01_NAV_V5",combinations=story || line=="JERUSALEM_A01_NAV_V4",items=combinations || line=="JERUSALEM_A01_NAV_V3",audio=items || line=="JERUSALEM_A01_NAV_V2";
  SceneGraph g;
  while (std::getline(lines,line)) {
    if (line.empty()) continue;
    std::istringstream row(line); row.imbue(std::locale::classic()); std::string kind,extra; row>>kind;
    if (kind=="node") {
      SceneNode n{};
      if (!(row>>n.id>>n.asset>>n.channels) || (row>>extra) || g.nodes.size()>=64) throw ParseError("invalid node model row");
      g.nodes.push_back(std::move(n));
    } else if (kind=="exit") {
      Exit e{}; unsigned puzzle,enabled;
      if (!(row>>e.source>>e.slot>>e.target>>puzzle>>enabled>>e.left>>e.top>>e.right>>e.bottom>>e.cursor>>e.event
               >>e.arrival.yaw>>e.arrival.pitch>>e.arrival.fov) || puzzle>1 || enabled>1 || (row>>extra) || g.exits.size()>=512)
        throw ParseError("invalid exit model row");
      e.puzzle=puzzle!=0; e.enabled=enabled!=0; g.exits.push_back(e);
    } else if (kind=="ambient" && audio) {
      SceneAmbient a{};
      if (!(row>>a.node>>a.sound>>a.volume>>a.stem) || (row>>extra) || g.ambient.size()>=192)
        throw ParseError("invalid ambient model row");
      g.ambient.push_back(std::move(a));
    } else if(kind=="pickup-set" && story) {
      std::uint32_t id,value;if(!(row>>id>>value) || (row>>extra))throw ParseError("invalid assignment pickup row");
      auto found=std::find_if(g.items.begin(),g.items.end(),[&](const Collectible& c){return c.id==id;});if(found==g.items.end() || found->assign)throw ParseError("unknown/repeated assignment pickup");found->assign=true;found->value=value;
    } else if(kind=="presentation" && story) {
      std::uint32_t object,index;ItemChannel b;if(!(row>>object>>index>>b.node>>b.channel) || (row>>extra))throw ParseError("invalid presentation row");
      auto found=std::find_if(g.presentations.begin(),g.presentations.end(),[&](const Presentation& p){return p.object==object && p.index==index;});
      if(found==g.presentations.end()){if(g.presentations.size()>=32)throw ParseError("presentation count limit");g.presentations.push_back({object,index,{}});found=std::prev(g.presentations.end());}if(found->channels.size()>=64)throw ParseError("presentation binding limit");found->channels.push_back(b);
    } else if(kind=="pickup-switch" && story) {
      PickupSwitch r{};if(!(row>>r.item>>r.variable>>r.value>>r.object>>r.hide>>r.show) || (row>>extra) || g.pickup_switches.size()>=32)throw ParseError("invalid pickup switch row");g.pickup_switches.push_back(r);
    } else if(kind=="target" && story) {
      InteractionTarget t{};unsigned enabled;if(!(row>>t.id>>t.node>>t.left>>t.top>>t.right>>t.bottom>>t.cursor>>t.event>>enabled>>std::quoted(t.name)) || (row>>extra) || enabled>1 || g.targets.size()>=32)throw ParseError("invalid target row");t.enabled=enabled!=0;g.targets.push_back(t);
    } else if(kind=="use" && story) {
      ItemUse r{};unsigned consume;if(!(row>>r.id>>r.target>>r.item>>r.variable>>r.value>>r.enable>>r.choice>>r.object>>r.zero>>r.nonzero>>r.dialogue>>consume) || (row>>extra) || consume>1 || g.uses.size()>=32)throw ParseError("invalid use row");r.consume=consume!=0;g.uses.push_back(r);
    } else if(kind=="dialogue" && story) {
      DialogueResource d{};if(!(row>>d.id>>d.area>>d.stem) || (row>>extra) || g.dialogues.size()>=32)throw ParseError("invalid dialogue row");g.dialogues.push_back(d);
    } else if(kind=="combine" && combinations) {
      Combination c{};
      if(!(row>>c.first>>c.second>>c.result>>std::quoted(c.name)) || (row>>extra) || g.combinations.size()>=16)throw ParseError("invalid combination row");
      g.combinations.push_back(std::move(c));
    } else if(kind=="item" && items) {
      Collectible item{};
      if(!(row>>item.id>>item.node>>item.left>>item.top>>item.right>>item.bottom>>item.cursor>>item.event>>item.counter>>item.milestone>>std::quoted(item.name)) || (row>>extra) || g.items.size()>=16) throw ParseError("invalid collectible row");
      g.items.push_back(std::move(item));
    } else if(kind=="item-channel" && items) {
      std::uint32_t id;ItemChannel b{};
      if(!(row>>id>>b.node>>b.channel) || (row>>extra)) throw ParseError("invalid collectible channel row");
      const auto item=std::find_if(g.items.begin(),g.items.end(),[&](const Collectible& c){return c.id==id;});
      if(item==g.items.end() || item->channels.size()>=64) throw ParseError("collectible channel has no bounded owner");
      item->channels.push_back(b);
    } else throw ParseError("unknown navigation model row");
  }
  validate_navigation(g); return g;
}

bool exit_contains(const Exit& e,std::int32_t x,std::int32_t y) {
  return e.enabled && x>=e.left && x<e.right && y>=e.top && y<e.bottom;
}
std::optional<std::size_t> navigation_hit(const SceneGraph& g,std::uint32_t source,std::int32_t x,std::int32_t y) {
  for (std::size_t i=0;i<g.exits.size();++i) if (g.exits[i].source==source && exit_contains(g.exits[i],x,y)) return i;
  return std::nullopt;
}
std::map<std::uint32_t,std::filesystem::path> resolve_scene_assets(const std::filesystem::path& root,const SceneGraph& g) {
  validate_navigation(g);
  const auto lower=[](std::string s){for (auto& c:s) if (c>='A'&&c<='Z') c=char(c-'A'+'a'); return s;};
  std::map<std::string,std::uint32_t> wanted;
  for (const auto& node:g.nodes) wanted.emplace(lower(node.asset+".aqc"),node.id);
  std::map<std::uint32_t,std::filesystem::path> found;
  std::size_t visited=0;
  for (auto it=std::filesystem::recursive_directory_iterator(root);it!=std::filesystem::recursive_directory_iterator();++it) {
    if (++visited>10000 || it.depth()>32) throw ParseError("scene asset scan limit exceeded");
    if (it->is_symlink() || !it->is_regular_file()) continue;
    const auto match=wanted.find(lower(it->path().filename().string()));
    if (match==wanted.end()) continue;
    if (!found.emplace(match->second,std::filesystem::canonical(it->path())).second)
      throw ParseError("duplicate scene asset: "+it->path().filename().string());
  }
  for (const auto& node:g.nodes) if (!found.count(node.id))
    throw ParseError("missing scene asset "+node.asset+".AQC; import the complete original DATA folder");
  return found;
}
std::filesystem::path resolve_ambient_asset(const std::filesystem::path& node,const std::string& stem) {
  if (stem.empty() || stem.size()>63) throw ParseError("invalid ambient resource stem");
  const auto upper=[](std::string s){for(auto& c:s) if(c>='a'&&c<='z') c=char(c-'a'+'A');return s;};
  for (const auto c:stem) if (!((c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_')) throw ParseError("unsafe ambient resource stem");
  auto area=node.parent_path();
  if (upper(area.filename().string())=="NODE") area=area.parent_path();
  std::map<std::string,std::filesystem::path> matches; std::size_t count=0;
  for (auto it=std::filesystem::recursive_directory_iterator(area);it!=std::filesystem::recursive_directory_iterator();++it) {
    if (++count>10000 || it.depth()>32) throw ParseError("ambient asset scan limit exceeded");
    if (it->is_symlink() || !it->is_regular_file()) continue;
    const auto file=upper(it->path().filename().string());
    for (const auto suffix:{".WAS",".WAC",".WAV"}) if (file==stem+suffix)
      if (!matches.emplace(suffix,std::filesystem::canonical(it->path())).second) throw ParseError("duplicate ambient asset: "+file);
  }
  for (const auto suffix:{".WAS",".WAC",".WAV"}) if (matches.count(suffix)) return matches.at(suffix);
  throw ParseError("missing scene sound: "+stem+" (.WAS/.WAC/.WAV); import the original area SOUND folder");
}
HotspotPoint hotspot_point(const PanoramaGeometry& g,TexturePoint p) {
  if (!g.width || !g.height || g.width>8192 || g.height>8192 || !std::isfinite(p.x) || !std::isfinite(p.y) ||
      g.yaw_min!=0 || g.yaw_max!=360 || !std::isfinite(g.pitch_min) || !std::isfinite(g.pitch_max) ||
      g.pitch_min< -90 || g.pitch_max>90 || g.pitch_min>=g.pitch_max) throw ParseError("invalid hotspot texture point");
  const auto u=std::floor(std::clamp(p.x,0.0,double(g.width)));
  const auto x=u==g.width ? 0 : u;
  const auto y=std::floor(std::clamp(p.y,0.0,double(g.height-1)));
  return {static_cast<std::int32_t>(x/g.width*3600),
          static_cast<std::int32_t>(((y-g.height/2.0)/g.height*(g.pitch_max-g.pitch_min)+(g.pitch_min+g.pitch_max)/2)*10)};
}
}
