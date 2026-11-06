#include "jerusalem/stage.hpp"
#include <algorithm>
namespace jerusalem {
A01Story::A01Story(StageSetup setup,SceneGraph graph,InteractionState& items):setup_(std::move(setup)),graph_(std::move(graph)),items_(items) {
  if(!items_.history().empty() || !items_.inventory().empty())throw ParseError("story requires a fresh inventory");
  for(const auto& n:graph_.nodes)channels_[n.id].resize(n.channels,false);
  for(const auto& a:setup_.access)enabled_.push_back(a.enabled);
  for(const auto id:{10030U,10002U,10003U,10004U,10001U,10005U,10018U,10020U,10031U})show(id,0,true);
}
void A01Story::push(StoryCue::Kind kind,std::uint32_t id,const std::string& asset,Camera camera){pending_.push_back({kind,id,asset,camera,++serial_});}
void A01Story::show(std::uint32_t object,std::uint32_t pres,bool on){
  if(on)shown_.insert({object,pres});else shown_.erase({object,pres});
  // The original writes flags directly, even when two objects share a channel.
  for(const auto& l:setup_.layers)if(l.object==object && l.presentation==pres && l.panorama)channels_.at(l.scene).at(l.channel)=on;
}
void A01Story::all(std::uint32_t object,bool on){for(const auto& l:setup_.layers)if(l.object==object)show(object,l.presentation,on);}
void A01Story::enable(std::uint32_t object,bool on){for(std::size_t i=0;i<setup_.access.size();++i)if(setup_.access[i].object==object)enabled_[i]=on;}
void A01Story::access(std::uint32_t object,std::uint32_t index,bool on){for(std::size_t i=0;i<setup_.access.size();++i)if(setup_.access[i].object==object && setup_.access[i].index==index)enabled_[i]=on;}
void A01Story::character(std::uint32_t object){all(object,false);show(object,items_.collected(10001)?0:1,true);}
void A01Story::start(){if(serial_)throw ParseError("story already started");push(StoryCue::Movie,1,"A01S01_AN001");push(StoryCue::Puzzle,10045);push(StoryCue::Speech,10999);}
std::optional<StoryCue> A01Story::next(){
  if(playing_ || pending_.empty())return {};
  auto cue=pending_.front();pending_.pop_front();
  if(cue.kind==StoryCue::Node){scene_=cue.id;puzzle_=false;}
  else if(cue.kind==StoryCue::Puzzle){scene_=cue.id;puzzle_=true;}
  else if(cue.kind==StoryCue::Boundary){boundary_=true;}
  if(cue.kind==StoryCue::Speech || cue.kind==StoryCue::Movie)playing_=cue;
  return cue;
}
bool A01Story::complete(std::uint64_t serial){
  if(!playing_ || playing_->serial!=serial)return false;
  const auto cue=*playing_;playing_.reset();
  if(cue.kind!=StoryCue::Speech)return true;
  // Verified dialogue-finished edges: original 0044c1c0, opening 0044efd0.
  switch(cue.id){
    case 10999:push(StoryCue::Movie,2,"A01S01_AN002");push(StoryCue::Node,1010101);break;
    case 10000:push(StoryCue::Puzzle,10018);push(StoryCue::Speech,10001);break;
    case 10001:push(StoryCue::Puzzle,10017);push(StoryCue::Speech,10002);break;
    case 10002:push(StoryCue::Node,1010106);show(10014,0,true);deadline_=clock_+60000;break;
    case 11004:push(StoryCue::Speech,11005);break;
    case 11005:push(StoryCue::Node,1010105,"",{24,0,87});break;
    case 11002:push(StoryCue::Movie,6,"A01S01_AN006");push(StoryCue::Puzzle,10043);break;
    case 11011:items_.inventory_.clear();push(StoryCue::Puzzle,10021);push(StoryCue::Speech,11012);break;
    case 11012:push(StoryCue::Movie,9,"A01S01_AN009");push(StoryCue::Puzzle,10027);push(StoryCue::Speech,11013);break;
    case 11013:push(StoryCue::Speech,11014);break;
    case 11014:push(StoryCue::Puzzle,10028);push(StoryCue::Speech,11015);break;
    case 11015:push(StoryCue::Speech,11016);break;
    case 11016:push(StoryCue::Puzzle,10026);push(StoryCue::Speech,11017);break;
    case 11017:push(StoryCue::Boundary,10023,"First escape complete. The next conversation/topic puzzle is not implemented yet.");break;
    default:break;
  }
  return true;
}
void A01Story::tick(std::uint64_t elapsed){
  // Original synchronous movies suspend the game loop; app background time is excluded by caller.
  if(playing_ && playing_->kind==StoryCue::Movie)return;
  if(elapsed>3600000)throw ParseError("story clock delta exceeds limit");
  clock_+=elapsed;
  if(deadline_ && clock_>=deadline_){
    deadline_=0;failed_=true;pending_.clear();playing_.reset();
    if(deadline_kind_==1){push(StoryCue::Puzzle,10038);push(StoryCue::Speech,11002);}
    else if(deadline_kind_==2){push(StoryCue::Puzzle,10019);deadline_=clock_+2000;deadline_kind_=3;}
    else push(StoryCue::Puzzle,10043);
  }
}
bool A01Story::enter_node(std::uint32_t id,Camera camera){
  if(busy() || failed_ || boundary_)return false;
  (void)scene_node(graph_,id);
  if(puzzle_ || std::none_of(graph_.exits.begin(),graph_.exits.end(),[&](const Exit& e){return e.source==scene_ && e.target==id && e.enabled && !e.puzzle;}))return false;
  if(id==1010103 && phase_==2){phase_=3;deadline_=clock_+50000;}
  push(StoryCue::Node,id,"",camera);
  if(id==1010106 && phase_==0){phase_=1;all(10011,true);enable(10011,true);push(StoryCue::Movie,3,"A01S01_AN003");push(StoryCue::Puzzle,10016);push(StoryCue::Speech,10000);}
  return true;
}
bool A01Story::enter_puzzle(std::uint32_t id){
  if(busy() || failed_ || boundary_ || id!=10039 || phase_!=1 || scene_!=1010106)return false;
  phase_=2;show(10014,0,false);show(10014,1,true);
  // 42bf70/90 enable/disable inclusive ranges of connection indices.
  for(auto& e:graph_.exits) {
    if((e.source==1010102 || e.source==1010103 || e.source==1010105) && e.slot==0)e.enabled=false;
    if(e.source==1010104 && e.slot<=1)e.enabled=false;
    if(e.source==1010106)e.enabled=e.slot==0;
  }
  push(StoryCue::Puzzle,10039);push(StoryCue::Speech,11004);return true;
}
bool A01Story::pickup(std::uint32_t object){
  if(busy() || failed_ || boundary_ || puzzle_ || collectible(graph_,object).node!=scene_ || !items_.pickup(object))return false;
  show(object,0,false);enable(object,false);
  if(object==10001 && character_){character(character_==1?10006:character_==2?10008:10009);}
  return true;
}
bool A01Story::activate(std::uint32_t object,std::uint32_t event,std::optional<std::uint32_t> item){
  if(busy() || failed_ || boundary_)return false;
  const auto active=hotspots();
  if(std::none_of(active.begin(),active.end(),[&](const StageAccess& h){return h.object==object && h.event==event;}))return false;
  if(item && (*item==10010 ? local_item()!=10010 : !items_.owns(*item)))return false;
  if(object==10000 && event==0 && item && (*item==10002 || *item==10004 || *item==10017)){push(StoryCue::Speech,11000);return true;}
  if(object==10007){
    if(item && *item==10003 && event==0){
      for(const auto& u:graph_.uses)if(u.target==10007 && u.item==10003 && items_.use(u.id)){character_=1;character(10006);enable(10006,true);return true;}
      return false;
    }
    if(!item && event==2){push(StoryCue::Node,1010105,"",{290,0,87});return true;}
    if(!item && phase_==0){push(StoryCue::Puzzle,10001);return true;}
    if(!item && (event==0 || event==1)){push(StoryCue::Speech,11008);return true;}
  }
  if(object==10006 && event==0 && item==10010){character_=2;all(10006,false);enable(10006,false);character(10008);enable(10010,false);enable(10008,true);return true;}
  if(object==10008 && !item && event==0){character_=3;all(10008,false);enable(10008,false);character(10009);enable(10009,true);access(10007,1,true);return true;}
  if(object==10009 && !item && event==0){character_=2;all(10009,false);enable(10009,false);character(10008);enable(10008,true);return true;}
  if(object==10011 && event==0 && item==10001 && character_>1 && phase_>1){
    all(10009,false);enable(10009,false);all(10008,true);access(10008,1,true);enable(10007,false);all(10012,true);all(10013,true);all(10005,false);deadline_=0;
    push(StoryCue::Movie,4,"A01S01_AN004");push(StoryCue::Puzzle,10000);
    if(!items_.owns(10004) && !items_.owns(10017)){deadline_=clock_+8000;deadline_kind_=2;}
    return true;
  }
  if(object==10008 && event==1 && !item){all(10008,false);enable(10008,false);all(10005,true);return true;}
  if(object==10005 && event==0 && item && (*item==10017 || *item==10004)){
    all(10015,true);show(10014,2,true);show(10018,0,true);show(10020,0,true);all(10030,true);
    push(StoryCue::Movie,7,"A01S01_AN007C");push(StoryCue::Puzzle,10020);push(StoryCue::Speech,11011);return true;
  }
  if((object==10012 || object==10013) && item){deadline_=0;failed_=true;push(StoryCue::Movie,object,"A01S01_AN007"+std::string(object==10012?"A":"B"));push(StoryCue::Puzzle,10044);return true;}
  if(object==10032 && !item && failed_){return false;}
  return false;
}
std::vector<StageAccess> A01Story::hotspots() const {
  std::vector<StageAccess> result;
  if(busy() || failed_ || boundary_)return result;
  for(std::size_t i=0;i<setup_.access.size();++i)if(enabled_[i] && setup_.access[i].scene==scene_ && setup_.access[i].panorama!=puzzle_)result.push_back(setup_.access[i]);
  return result;
}
std::optional<StageAccess> A01Story::hit(int x,int y) const{for(const auto& a:hotspots())if(x>=a.left && x<a.right && y>=a.top && y<a.bottom)return a;return {};}
std::vector<StageLayer> A01Story::layers() const {
  std::vector<StageLayer> result;
  for(const auto& l:setup_.layers)if(l.scene==scene_ && l.panorama!=puzzle_ && shown_.count({l.object,l.presentation}) && (!l.panorama || channels_.at(l.scene).at(l.channel)))result.push_back(l);
  std::stable_sort(result.begin(),result.end(),[](const StageLayer& a,const StageLayer& b){return a.order>b.order;});return result;
}
std::vector<bool> A01Story::visibility(std::uint32_t node) const {return channels_.at(node);}
std::optional<std::uint32_t> A01Story::local_item() const {
  if(busy() || failed_ || puzzle_ || scene_!=1010105 || character_!=1)return {};
  for(std::size_t i=0;i<setup_.access.size();++i)if(setup_.access[i].object==10010 && enabled_[i])return 10010;
  return {};
}
}
