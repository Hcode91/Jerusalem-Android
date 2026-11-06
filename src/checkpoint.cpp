#include "jerusalem/checkpoint.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
namespace jerusalem {
namespace {
constexpr std::uint32_t format_version=1;
constexpr std::size_t header_size=64,max_size=header_size+64*4+4;
class Fingerprint {
  std::uint64_t value_=14695981039346656037ULL;
 public:
  void bytes(Bytes b){for(std::size_t i=0;i<b.size();++i)value_=(value_^b.u8(i))*1099511628211ULL;}
  void word(std::uint64_t v){for(unsigned i=0;i<8;++i){const auto byte=static_cast<std::uint8_t>(v>>(8*i));bytes(Bytes(&byte,1));}}
  std::uint64_t value() const{return value_;}
};
void integer(std::vector<std::uint8_t>& out,std::uint64_t value,unsigned width){for(unsigned i=0;i<width;++i)out.push_back(static_cast<std::uint8_t>(value>>(8*i)));}
std::uint64_t integer64(Bytes b,std::size_t p){return b.u32(p)|(std::uint64_t(b.u32(p+4))<<32);}
void real(std::vector<std::uint8_t>& out,double value){
  static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559,"checkpoint requires IEEE754 binary64");
  std::uint64_t bits;std::memcpy(&bits,&value,8);integer(out,bits,8);
}
double real(Bytes b,std::size_t p){const auto bits=integer64(b,p);double value;std::memcpy(&value,&bits,8);return value;}
std::uint32_t crc32(Bytes b){
  std::uint32_t crc=0xffffffffU;
  for(std::size_t i=0;i<b.size();++i){crc^=b.u8(i);for(unsigned j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0xedb88320U:0U);}
  return crc^0xffffffffU;
}
void validate(const SceneGraph& graph,const Checkpoint& cp){
  validate_navigation(graph);scene_node(graph,cp.node);
  if(graph.items.empty())throw ParseError("checkpoint requires pickup connections; refresh A01 navigation");
  const auto& c=cp.camera;
  if(!std::isfinite(c.yaw) || c.yaw<0 || c.yaw>=360 || !std::isfinite(c.pitch) || std::abs(c.pitch)>90 ||
     !std::isfinite(c.fov) || c.fov<30 || c.fov>87 || cp.inventory.size()>graph.items.size()+graph.combinations.size()+graph.uses.size())throw ParseError("invalid checkpoint camera/inventory");
  InteractionState replay(graph);
  for(const auto action:cp.inventory) {
    if(has_story_rules(graph) && (action&0xc0000000U)==0x40000000U) {
      const auto id=action&0x3fffffffU;
      const auto rule=std::find_if(graph.uses.begin(),graph.uses.end(),[&](const ItemUse& r){return r.id==id && r.consume;});
      if(rule==graph.uses.end() || !replay.use(id))throw ParseError("invalid checkpoint item-use sequence");
    }else if(action&0x80000000U) {
      const auto id=action&0x7fffffffU;
      const auto rule=std::find_if(graph.combinations.begin(),graph.combinations.end(),[&](const Combination& c){return c.result==id;});
      if(rule==graph.combinations.end() || !replay.combine(rule->first,rule->second))throw ParseError("invalid checkpoint combination sequence");
    }else if(!replay.pickup(action))throw ParseError("duplicate checkpoint pickup");
  }
}
}
std::uint64_t checkpoint_rules(const SceneGraph& graph){
  auto stable=graph;for(auto& item:stable.items)item.name="ITEM";for(auto& c:stable.combinations)c.name="ITEM";for(auto& t:stable.targets)t.name="TARGET"; // Display translations do not change game state compatibility.
  const auto model=write_navigation(stable);
  Fingerprint fingerprint;fingerprint.word(has_story_rules(graph)?3:graph.combinations.empty()?1:2);fingerprint.bytes(Bytes(reinterpret_cast<const std::uint8_t*>(model.data()),model.size()));return fingerprint.value();
}
std::uint64_t checkpoint_assets(const SceneGraph& graph,const std::map<std::uint32_t,std::filesystem::path>& assets){
  validate_navigation(graph);
  if(assets.size()!=graph.nodes.size())throw ParseError("incomplete checkpoint asset set");
  Fingerprint fingerprint;std::uint64_t total=0;
  for(const auto& entry:assets){
    scene_node(graph,entry.first);
    const auto size=std::filesystem::file_size(entry.second);
    if(size>max_resource_bytes || total+size>256*1024*1024)throw ParseError("checkpoint asset fingerprint limit exceeded");
    const auto bytes=read_file(entry.second);
    if(bytes.size()!=size)throw ParseError("scene asset changed during checkpoint validation");
    total+=size;fingerprint.word(entry.first);fingerprint.word(size);fingerprint.bytes(Bytes(bytes));
  }
  return fingerprint.value();
}
InteractionState restore_interactions(const SceneGraph& graph,const Checkpoint& cp){
  validate(graph,cp);InteractionState state(graph);for(const auto action:cp.inventory) {
    if(has_story_rules(graph) && (action&0xc0000000U)==0x40000000U)state.use(action&0x3fffffffU);
    else if(action&0x80000000U) {
      const auto& rule=*std::find_if(graph.combinations.begin(),graph.combinations.end(),[&](const Combination& c){return c.result==(action&0x7fffffffU);});
      state.combine(rule.first,rule.second);
    }else state.pickup(action);
  }return state;
}
std::vector<std::uint8_t> encode_checkpoint(const SceneGraph& graph,std::uint64_t assets,const Checkpoint& cp){
  validate(graph,cp);
  std::vector<std::uint8_t> out{'J','R','N','S','A','V','E',0};
  integer(out,format_version,4);integer(out,has_story_rules(graph)?3:graph.combinations.empty()?1:2,4);integer(out,checkpoint_rules(graph),8);integer(out,assets,8);
  integer(out,cp.node,4);integer(out,cp.inventory.size(),4);real(out,cp.camera.yaw);real(out,cp.camera.pitch);real(out,cp.camera.fov);
  for(const auto id:cp.inventory)integer(out,id,4);
  integer(out,crc32(Bytes(out)),4);return out;
}
Checkpoint decode_checkpoint(Bytes b,const SceneGraph& graph,std::uint64_t assets){
  if(b.size()<header_size+4 || b.size()>max_size || !b.is(0,std::string("JRNSAVE\0",8)))throw ParseError("not a supported native A01 checkpoint");
  if(b.u32(8)!=format_version || (b.u32(12)<1 || b.u32(12)>3))throw ParseError("unsupported native checkpoint version/state revision");
  const auto count=b.u32(36);
  if(count>64 || b.size()!=header_size+std::size_t(count)*4+4)throw ParseError("truncated or malformed checkpoint length");
  if(crc32(b.slice(0,b.size()-4))!=b.u32(b.size()-4))throw ParseError("checkpoint checksum mismatch");
  auto compatible=legacy_navigation(graph,b.u32(12));
  if(integer64(b,16)!=checkpoint_rules(compatible))throw ParseError("checkpoint scene rules differ from the current connections");
  if(integer64(b,24)!=assets)throw ParseError("checkpoint original node data differs from imported assets");
  Checkpoint cp{b.u32(32),{real(b,40),real(b,48),real(b,56)},{}};
  for(std::size_t i=0;i<count;++i){const auto action=b.u32(header_size+i*4);if((b.u32(12)==1 && (action&0x80000000U)) || (b.u32(12)<3 && has_story_rules(graph) && (action&0xc0000000U)==0x40000000U))throw ParseError("combination in legacy checkpoint");cp.inventory.push_back(action);}
  validate(graph,cp);return cp;
}
void write_checkpoint(const std::filesystem::path& path,const SceneGraph& graph,std::uint64_t assets,const Checkpoint& cp){
  const auto bytes=encode_checkpoint(graph,assets,cp); // Validate before touching the existing checkpoint.
#ifdef _WIN32
  (void)path;(void)bytes;throw ParseError("atomic checkpoint file writes currently support Linux and Android");
#else
  const auto parent=path.parent_path().empty()?std::filesystem::path("."):path.parent_path();
  std::filesystem::create_directories(parent);
  std::string temporary=(parent/("."+path.filename().string()+".XXXXXX")).string();
  int fd=::mkstemp(temporary.data());
  if(fd<0)throw ParseError("cannot create checkpoint temporary file: "+std::string(std::strerror(errno)));
  try{
    std::size_t position=0;
    while(position<bytes.size()){
      const auto written=::write(fd,bytes.data()+position,bytes.size()-position);
      if(written<0 && errno==EINTR)continue;
      if(written<=0)throw ParseError("cannot write checkpoint: "+std::string(std::strerror(errno)));
      position+=static_cast<std::size_t>(written);
    }
    int synced;do{synced=::fsync(fd);}while(synced<0 && errno==EINTR);
    if(synced<0)throw ParseError("cannot sync checkpoint: "+std::string(std::strerror(errno)));
    const auto closed=::close(fd);fd=-1;
    if(closed<0)throw ParseError("cannot close checkpoint temporary file");
    std::filesystem::rename(temporary,path);
    const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY);
    if(directory>=0){::fsync(directory);::close(directory);} // Some filesystems do not support directory fsync.
  }catch(...){if(fd>=0)::close(fd);std::error_code error;std::filesystem::remove(temporary,error);throw;}
#endif
}
Checkpoint read_checkpoint(const std::filesystem::path& path,const SceneGraph& graph,std::uint64_t assets){
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  if(!input)throw ParseError("no readable native A01 checkpoint; use Save checkpoint first");
  const auto size=input.tellg();
  if(size<static_cast<std::streamoff>(header_size+4) || size>static_cast<std::streamoff>(max_size))throw ParseError("invalid native checkpoint file size");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));input.seekg(0);
  if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())) || input.peek()!=std::char_traits<char>::eof())throw ParseError("checkpoint changed or could not be read completely");
  return decode_checkpoint(Bytes(bytes),graph,assets);
}
}
