#include "jerusalem/interactions.hpp"
#include <algorithm>
namespace jerusalem {
PickupEffect parse_pickup_effect(Bytes b,std::uint32_t address) {
  if (b.size()!=79 || std::uint64_t(address)+79>0x100000000ULL) throw ParseError("unsupported pickup packet size");
  const auto exact=[&](std::size_t p,const std::string& bytes){if(!b.is(p,bytes)) throw ParseError("unsupported pickup instruction at "+std::to_string(p));};
  const auto call=[&](std::size_t p,std::uint32_t target){if(b.u8(p)!=0xe8 || std::int64_t(address)+p+5+b.i32(p+1)!=target) throw ParseError("unsupported pickup API");};
  exact(0,std::string("\x68",1)); exact(5,std::string("\x8b\xce",2));call(7,0x42c1d0);
  exact(12,std::string("\xfe\xc0\x8b\xce\x50\x68",6));call(22,0x42c1a0);
  exact(27,std::string("\x6a",1));call(29,0x432440);
  exact(34,std::string("\x83\xc4\x04\x8b\xce\x6a\x00\x68",8));call(46,0x42a9a0);
  exact(51,std::string("\x68",1));exact(56,std::string("\x8b\xce",2));call(58,0x429d70);
  exact(63,std::string("\x68",1));exact(68,std::string("\x8b\xce",2));call(70,0x42c3a0);
  exact(75,std::string("\x5e\xc2\x18\x00",4));
  const auto object=b.u32(42),counter=b.u32(1);
  if (!object || object>0x7fffffffU || !counter || counter>0x7fffffffU || b.u32(18)!=counter ||
      b.u32(52)!=object || b.u32(64)!=object || b.u8(28)>127) throw ParseError("inconsistent pickup action identities");
  return {object,counter,b.u8(28)};
}
const Collectible& collectible(const SceneGraph& graph,std::uint32_t id) {
  const auto found=std::find_if(graph.items.begin(),graph.items.end(),[&](const Collectible& c){return c.id==id;});
  if(found==graph.items.end()) throw ParseError("unknown collectible");
  return *found;
}
InteractionState::InteractionState(const SceneGraph& graph):graph_(graph) {
  validate_navigation(graph_);
  for(const auto& node:graph_.nodes) visibility_[node.id]=std::vector<bool>(node.channels,false);
  for(const auto& item:graph_.items) {
    variables_[item.counter]=0;
    for(const auto& binding:item.channels) visibility_.at(binding.node)[binding.channel]=true;
  }
}
bool InteractionState::collected(std::uint32_t id) const {collectible(graph_,id);return collected_.count(id)!=0;}
std::optional<std::uint32_t> InteractionState::hit(std::uint32_t node,std::int32_t x,std::int32_t y) const {
  scene_node(graph_,node);
  for(const auto& item:graph_.items) if(item.node==node && !collected(item.id) && x>=item.left && x<item.right && y>=item.top && y<item.bottom) return item.id;
  return std::nullopt;
}
bool InteractionState::pickup(std::uint32_t id) {
  const auto& item=collectible(graph_,id);
  if(collected(id)) return false;
  // Original presentation hide writes channel flags directly, including shared channels.
  for(const auto& binding:item.channels) visibility_.at(binding.node)[binding.channel]=false;
  variables_.at(item.counter)=static_cast<std::uint8_t>(variables_.at(item.counter)+1);
  collected_.insert(id);inventory_.push_back(id);milestones_.insert(item.milestone);
  return true;
}
const std::vector<bool>& InteractionState::visibility(std::uint32_t node) const {scene_node(graph_,node);return visibility_.at(node);}
std::uint8_t InteractionState::variable(std::uint32_t key) const {
  const auto found=variables_.find(key);if(found==variables_.end()) throw ParseError("untracked pickup variable");return found->second;
}
Bitmap compose_items(const AqcAnimation& animation,const Bitmap& base,const std::vector<bool>& visible) {
  if(visible.size()!=animation.channels().size() || base.width!=animation.info().width || base.height!=animation.info().height ||
     std::uint64_t(base.width)*base.height*4!=base.rgba.size()) throw ParseError("item visibility/base geometry mismatch");
  auto image=base;
  for(std::size_t i=0;i<visible.size();++i) if(visible[i]) {
    const auto& channel=animation.channels()[i];
    if(channel.frames.size()!=1) throw ParseError("collectible presentation requires a single-frame channel");
    const auto& f=channel.frames[0];apply_aqc_patch(image,animation.frame(i,0),f.left,f.top);
  }
  return image;
}
}
