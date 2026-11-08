#include "jerusalem/stage.hpp"
#include "jerusalem/cinema.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace jerusalem {
namespace {
std::string upper(std::string s){for(auto& c:s)if(c>='a'&&c<='z')c=char(c-'a'+'A');return s;}
bool name_ok(const std::string& s){return !s.empty() && s.size()<96 && std::all_of(s.begin(),s.end(),[](char c){return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.';}) && s!="." && s!="..";}
void validate(const StageSetup& s) {
  if(s.backgrounds.empty() || s.backgrounds.size()>64 || s.access.size()>128 || s.layers.size()>256 || s.dialogues.size()>64)throw ParseError("invalid stage record count");
  std::set<std::uint32_t> backgrounds,dialogues;
  for(const auto& b:s.backgrounds)if(!b.id || !name_ok(b.image) || !backgrounds.insert(b.id).second || b.x<0 || b.y<0 || b.x>640 || b.y>480)throw ParseError("invalid stage background");
  std::set<std::pair<std::uint32_t,std::uint32_t>> accesses;
  for(const auto& a:s.access)if(!a.object || a.index>31 || !accesses.insert({a.object,a.index}).second || (a.panorama ? (a.scene<1010101 || a.scene>1010106 || a.left<0 || a.right>3600 || a.top< -900 || a.bottom>900) : (!backgrounds.count(a.scene) || a.left<0 || a.right>640 || a.top<0 || a.bottom>480)) || a.left>=a.right || a.top>=a.bottom || a.cursor>255 || a.event>255)throw ParseError("invalid stage access");
  for(const auto& l:s.layers)if(!l.object || l.presentation>31 || !l.frames || l.frames>64 || l.order>2000 || (l.panorama ? (l.scene<1010101 || l.scene>1010106 || l.channel>31) : (!backgrounds.count(l.scene) || l.channel>1 || !name_ok(l.image) || l.x<0 || l.x>=640 || l.y<0 || l.y>=480)) || !std::isfinite(l.fps) || (l.animated && (l.fps<=0 || l.fps>60 || (l.mode!=4 && l.mode!=16))) || (!l.animated && l.frames!=1))throw ParseError("invalid stage layer");
  for(const auto& d:s.dialogues)if(!d.id || d.area!="A01" || !name_ok(d.stem) || !dialogues.insert(d.id).second)throw ParseError("invalid stage dialogue");
  std::set<std::uint32_t> objects;
  if(s.objects.size()>128)throw ParseError("stage object count limit");
  for(const auto& o:s.objects)if(!o.id || !objects.insert(o.id).second || o.name.size()>96 || !std::all_of(o.name.begin(),o.name.end(),[](unsigned char c){return c>=32 && c<=126;}) || (!o.icon.empty() && !name_ok(o.icon)))throw ParseError("invalid stage object");
  std::set<std::pair<std::uint32_t,std::uint32_t>> sounds;
  if(s.ambient.size()>128)throw ParseError("stage sound count limit");
  for(const auto& a:s.ambient)if(!backgrounds.count(a.node) || !a.sound || a.volume>100 || a.master>100 || !name_ok(a.stem) || !sounds.insert({a.node,a.sound}).second)throw ParseError("invalid stage sound");
}
}
std::string write_stage(const StageSetup& s) {
  std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(12)<<(s.objects.empty() && s.ambient.empty()?"JERUSALEM_A01_STAGE_V1\n":"JERUSALEM_A01_STAGE_V2\n");
  for(const auto& b:s.backgrounds)out<<"background "<<b.id<<' '<<b.image<<' '<<b.x<<' '<<b.y<<'\n';
  for(const auto& a:s.access)out<<"access "<<a.object<<' '<<a.scene<<' '<<a.index<<' '<<a.panorama<<' '<<a.enabled<<' '<<a.left<<' '<<a.top<<' '<<a.right<<' '<<a.bottom<<' '<<a.cursor<<' '<<a.event<<'\n';
  for(const auto& l:s.layers)out<<"layer "<<l.object<<' '<<l.presentation<<' '<<l.scene<<' '<<l.panorama<<' '<<l.animated<<' '<<(l.image.empty()?"-":l.image)<<' '<<l.channel<<' '<<l.frames<<' '<<l.mode<<' '<<l.order<<' '<<l.x<<' '<<l.y<<' '<<l.fps<<'\n';
  for(const auto& d:s.dialogues)out<<"dialogue "<<d.id<<' '<<d.area<<' '<<d.stem<<'\n';
  for(const auto& o:s.objects)out<<"object "<<o.id<<' '<<std::quoted(o.name)<<' '<<std::quoted(o.icon)<<'\n';
  for(const auto& a:s.ambient)out<<"sound "<<a.node<<' '<<a.sound<<' '<<a.volume<<' '<<a.master<<' '<<a.stem<<'\n';
  return out.str();
}
StageSetup parse_stage(const std::string& text) {
  if(text.size()>128*1024 || text.find('\0')!=std::string::npos)throw ParseError("invalid stage text");
  std::istringstream in(text);in.imbue(std::locale::classic());std::string line,kind,extra;StageSetup s;
  if(!std::getline(in,line) || (line!="JERUSALEM_A01_STAGE_V1" && line!="JERUSALEM_A01_STAGE_V2"))throw ParseError("unsupported stage version");
  const bool extended=line=="JERUSALEM_A01_STAGE_V2";
  unsigned count=0;
  while(std::getline(in,line)) {
    if(++count>512 || line.size()>512)throw ParseError("stage text limit");
    std::istringstream row(line);row.imbue(std::locale::classic());if(!(row>>kind))continue;
    if(kind=="background"){StageBackground b{};if(!(row>>b.id>>b.image>>b.x>>b.y))throw ParseError("invalid background row");s.backgrounds.push_back(b);}
    else if(kind=="access"){StageAccess a{};if(!(row>>a.object>>a.scene>>a.index>>a.panorama>>a.enabled>>a.left>>a.top>>a.right>>a.bottom>>a.cursor>>a.event))throw ParseError("invalid access row");s.access.push_back(a);}
    else if(kind=="layer"){StageLayer l{};if(!(row>>l.object>>l.presentation>>l.scene>>l.panorama>>l.animated>>l.image>>l.channel>>l.frames>>l.mode>>l.order>>l.x>>l.y>>l.fps))throw ParseError("invalid layer row");if(l.image=="-")l.image.clear();s.layers.push_back(l);}
    else if(kind=="dialogue"){DialogueResource d{};if(!(row>>d.id>>d.area>>d.stem))throw ParseError("invalid dialogue row");s.dialogues.push_back(d);}
    else if(kind=="object" && extended){StageObject o{};if(!(row>>o.id>>std::quoted(o.name)>>std::quoted(o.icon)))throw ParseError("invalid object row");s.objects.push_back(o);}
    else if(kind=="sound" && extended){SceneAmbient a{};if(!(row>>a.node>>a.sound>>a.volume>>a.master>>a.stem))throw ParseError("invalid stage sound row");s.ambient.push_back(a);}
    else throw ParseError("unknown stage record");
    if(row>>extra)throw ParseError("extra stage field");
  }
  validate(s);return s;
}
std::filesystem::path game_file(const std::filesystem::path& root,const std::vector<std::string>& components) {
  if(components.empty() || components.size()>8)throw ParseError("invalid game path");
  const auto descend=[&](std::filesystem::path path)->std::optional<std::filesystem::path>{
    for(const auto& component:components) {
      if(!name_ok(upper(component)))throw ParseError("unsafe game path component");
      if(!std::filesystem::is_directory(path) || std::filesystem::is_symlink(path))return {};
      std::optional<std::filesystem::path> found;
      for(const auto& entry:std::filesystem::directory_iterator(path))if(upper(entry.path().filename().string())==upper(component)){
        if(found || entry.is_symlink())throw ParseError("ambiguous or symlinked game path: "+component);
        found=entry.path();
      }
      if(!found)return {};
      path=*found;
    }
    if(!std::filesystem::is_regular_file(path))return {};
    return path;
  };
  if(std::filesystem::is_symlink(root))throw ParseError("symlinked data root");
  std::vector<std::filesystem::path> bases{root};
  for(const auto& e:std::filesystem::directory_iterator(root))if(upper(e.path().filename().string())=="DATA"){
    if(bases.size()>1 || e.is_symlink())throw ParseError("ambiguous DATA directory");
    bases.push_back(e.path());
  }
  std::optional<std::filesystem::path> result;
  for(const auto& base:bases)if(const auto found=descend(base)){if(result)throw ParseError("duplicate game asset");result=found;}
  if(!result)throw ParseError("missing original game file: "+components.back());
  return *result;
}
StageArt::StageArt(std::vector<std::uint8_t> archive):data_(std::move(archive)),directory_(parse_at3(Bytes(data_))) {}
Bitmap StageArt::image(const std::string& name) const {
  if(!name_ok(upper(name)))throw ParseError("unsafe art name");
  const bool alpha=upper(name).size()>=4 && upper(name).substr(name.size()-4)==".TGA";
  const ArtEntry* found=nullptr;
  for(const auto& e:directory_.entries){const auto p=e.name.find_last_of("/\\");if(upper(e.name.substr(p==std::string::npos?0:p+1))==upper(name)){if(found)throw ParseError("ambiguous AT3 basename: "+name);found=&e;}}
  if(!found)throw ParseError("missing AT3 image: "+name);
  const auto b=Bytes(data_).slice(found->offset,found->stored_size);
  if(b.is(0,std::string("CNM UNR\0",8))){CinemaDecoder d(std::vector<std::uint8_t>(b.data(),b.data()+b.size()));if(d.info().frames!=1)throw ParseError("expected a single AT3 image");while(!d.done())d.next();return alpha?d.alpha_image():d.sprite();}
  if(alpha)throw ParseError("unsupported AT3 TGA encoding");
  return decode_bitmap(b);
}
void stage_blit(Bitmap& dst,const Bitmap& src,int x,int y) {
  if(!dst.width || !dst.height || dst.width>4096 || dst.height>4096 || src.width>4096 || src.height>4096 || std::uint64_t(dst.width)*dst.height*4!=dst.rgba.size() || !src.width || !src.height || std::uint64_t(src.width)*src.height*4!=src.rgba.size())throw ParseError("invalid stage bitmap");
  for(unsigned row=0;row<src.height;++row)for(unsigned col=0;col<src.width;++col){const auto dx=std::int64_t(x)+col,dy=std::int64_t(y)+row;if(dx<0 || dy<0 || dx>=dst.width || dy>=dst.height)continue;const auto s=(std::size_t(row)*src.width+col)*4,d=(std::size_t(dy)*dst.width+std::size_t(dx))*4;const unsigned a=src.rgba[s+3];for(unsigned c=0;c<3;++c)dst.rgba[d+c]=static_cast<std::uint8_t>((unsigned(src.rgba[s+c])*a+unsigned(dst.rgba[d+c])*(255-a)+127)/255);dst.rgba[d+3]=255;}
}
void stage_blit_tga(Bitmap& dst,const Bitmap& src,int x,int y) {
  if(!dst.width || !dst.height || dst.width>4096 || dst.height>4096 || !src.width || !src.height || src.width>4096 || src.height>4096 || std::uint64_t(dst.width)*dst.height*4!=dst.rgba.size() || std::uint64_t(src.width)*src.height*4!=src.rgba.size())throw ParseError("invalid alpha bitmap");
  for(unsigned row=0;row<src.height;++row)for(unsigned col=0;col<src.width;++col){const auto dx=std::int64_t(x)+col,dy=std::int64_t(y)+row;if(dx<0 || dy<0 || dx>=dst.width || dy>=dst.height)continue;const auto s=(std::size_t(row)*src.width+col)*4,d=(std::size_t(dy)*dst.width+std::size_t(dx))*4;const unsigned a=src.rgba[s+3];for(unsigned c=0;c<3;++c)dst.rgba[d+c]=static_cast<std::uint8_t>((unsigned(src.rgba[s+c])*a>>8)+(unsigned(dst.rgba[d+c])*(255-a)>>8));dst.rgba[d+3]=255;}
}
std::optional<HotspotPoint> stage_point(double x,double y,int w,int h) {
  if(w<=0 || h<=0 || !std::isfinite(x) || !std::isfinite(y))return {};
  const auto scale=std::min(w/640.0,h/480.0),left=(w-640*scale)/2,top=(h-480*scale)/2;
  x=(x-left)/scale;y=(y-top)/scale;if(x<0 || y<0 || x>=640 || y>=480)return {};return HotspotPoint{static_cast<int>(x),static_cast<int>(y)};
}
std::size_t stage_frame(std::uint64_t elapsed,const StageLayer& l) {
  if((l.animated && l.mode!=4 && l.mode!=16) || !l.frames || !std::isfinite(l.fps) || l.fps<0 || l.fps>60 || l.frames>64)throw ParseError("invalid stage animation");
  if(!l.animated || l.frames==1)return 0;
  // Only verified loop (4) / ping-pong (16) registrations are admitted.
  const auto period=l.mode==16?2*l.frames:l.frames;
  const auto tick=static_cast<std::uint64_t>(std::fmod(static_cast<long double>(elapsed)*l.fps/1000,period));
  return l.mode==16 && tick>=l.frames?period-1-tick:tick;
}
}
