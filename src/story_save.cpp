#include "jerusalem/stage.hpp"
#include "jerusalem/checkpoint.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace jerusalem {
namespace {
constexpr std::uint64_t max_time=365ULL*24*60*60*1000;
std::uint32_t crc(Bytes b){std::uint32_t c=0xffffffffU;for(std::size_t i=0;i<b.size();++i){c^=b.u8(i);for(unsigned j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320U:0);}return c^0xffffffffU;}
struct Writer {
 std::vector<std::uint8_t> b;
 void u(std::uint64_t v,unsigned n=4){for(unsigned i=0;i<n;++i)b.push_back(std::uint8_t(v>>(i*8)));}
 void real(double v){static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559);std::uint64_t bits;std::memcpy(&bits,&v,8);u(bits,8);}
 void camera(Camera c){real(c.yaw);real(c.pitch);real(c.fov);}
 void text(const std::string& s){if(s.size()>256)throw ParseError("story string limit");u(s.size());b.insert(b.end(),s.begin(),s.end());}
 void cue(const StoryCue& c){u(c.kind);u(c.id);text(c.asset);camera(c.camera);u(c.serial,8);}
 template<class T>void flags(const T& f){u(f.size());for(bool on:f)u(on,1);}
};
struct Reader {
 Bytes b;std::size_t p=0;
 std::uint64_t u(unsigned n=4){b.require(p,n);std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(b.u8(p++))<<(8*i);return v;}
 std::size_t count(std::size_t limit){const auto n=u();if(n>limit)throw ParseError("story count limit");return static_cast<std::size_t>(n);}
 bool flag(){const auto v=u(1);if(v>1)throw ParseError("invalid story flag");return v!=0;}
 double real(){auto bits=u(8);double value;std::memcpy(&value,&bits,8);return value;}
 Camera camera(){Camera c{real(),real(),real()};if(!std::isfinite(c.yaw)||std::abs(c.yaw)>1000000||!std::isfinite(c.pitch)||std::abs(c.pitch)>90||!std::isfinite(c.fov)||c.fov<30||c.fov>87)throw ParseError("invalid story camera");return c;}
 std::string text(){const auto n=count(256);const auto data=b.slice(p,n);p+=n;return std::string(reinterpret_cast<const char*>(data.data()),n);}
 StoryCue cue(){const auto kind=u();if(kind>StoryCue::Boundary)throw ParseError("invalid story cue kind");StoryCue c;c.kind=static_cast<StoryCue::Kind>(kind);c.id=static_cast<std::uint32_t>(u());c.asset=text();c.camera=camera();c.serial=u(8);return c;}
 std::vector<bool> flags(std::size_t n){if(count(n)!=n)throw ParseError("story flag geometry differs");std::vector<bool> f;for(std::size_t i=0;i<n;++i)f.push_back(flag());return f;}
};
std::uint64_t rules(const SceneGraph& graph,const StageSetup& setup){auto h=checkpoint_rules(graph);const auto s=write_stage(setup);for(unsigned char c:s)h=(h^c)*1099511628211ULL;return h;}
}
std::vector<std::uint8_t> A01Story::save(const StoryResume& resume,std::uint64_t assets) const {
 if(!serial_)throw ParseError("story has not started");
 Writer w;w.b={'J','R','N','S','T','O','R','Y'};w.u(1);w.u(rules(items_.graph_,setup_),8);w.u(assets,8);
 w.camera(resume.camera);w.u(resume.media_ms,8);w.u(resume.visual_ms,8);w.u(resume.selected.value_or(0));
 w.u(scene_);w.u(phase_);w.u(character_);w.u(deadline_kind_);w.u(serial_,8);w.u(clock_,8);w.u(deadline_,8);
 w.u(puzzle_,1);w.u(failed_,1);w.u(boundary_,1);w.u(bag_cleared_,1);
 w.u(items_.history_.size());for(auto action:items_.history_)w.u(action);
 w.u(shown_.size());for(auto p:shown_){w.u(p.first);w.u(p.second);}
 w.flags(enabled_);w.u(channels_.size());for(const auto& c:channels_){w.u(c.first);w.flags(c.second);}
 w.u(graph_.exits.size());for(const auto& e:graph_.exits)w.u(e.enabled,1);
 w.u(playing_.has_value(),1);if(playing_)w.cue(*playing_);
 w.u(pending_.size());for(const auto& c:pending_)w.cue(c);
 w.u(crc(Bytes(w.b)));if(w.b.size()>65536)throw ParseError("story save size limit");
 InteractionState checked_items(items_.graph_);A01Story checked(setup_,items_.graph_,checked_items);(void)checked.load(Bytes(w.b),assets);
 return w.b;
}
StoryResume A01Story::load(Bytes bytes,std::uint64_t assets) {
 if(bytes.size()<128 || bytes.size()>65536 || !bytes.is(0,"JRNSTORY"))throw ParseError("not a native story save");
 if(crc(bytes.slice(0,bytes.size()-4))!=bytes.u32(bytes.size()-4))throw ParseError("story save checksum mismatch");
 Reader r{bytes.slice(0,bytes.size()-4),8};if(r.u()!=1)throw ParseError("unsupported story save version");
 if(r.u(8)!=rules(items_.graph_,setup_))throw ParseError("story save rules differ; re-import matching game connections");
 if(r.u(8)!=assets)throw ParseError("story save game data differs from imported assets");
 StoryResume resume;resume.camera=r.camera();resume.media_ms=r.u(8);resume.visual_ms=r.u(8);const auto selected=r.u();if(selected)resume.selected=static_cast<std::uint32_t>(selected);
 const auto scene=r.u(),phase=r.u(),character=r.u(),kind=r.u(),serial=r.u(8),clock=r.u(8),deadline=r.u(8);
 const bool puzzle=r.flag(),failed=r.flag(),boundary=r.flag(),cleared=r.flag();
 if(phase>3 || character>3 || kind<1 || kind>3 || !serial || serial>1000000000 || clock>max_time || resume.visual_ms>max_time || resume.media_ms>24ULL*60*60*1000 || (deadline && (deadline<=clock || deadline-clock>60000)))throw ParseError("invalid story phase/clock");
 if(scene){if(puzzle){if(std::none_of(setup_.backgrounds.begin(),setup_.backgrounds.end(),[&](const StageBackground& b){return b.id==scene;}))throw ParseError("unknown saved puzzle");}else scene_node(items_.graph_,static_cast<std::uint32_t>(scene));}
 else if(puzzle || phase)throw ParseError("invalid opening scene");
 InteractionState restored(items_.graph_);
 const auto history_count=r.count(64);std::vector<std::uint32_t> history;for(std::size_t i=0;i<history_count;++i)history.push_back(static_cast<std::uint32_t>(r.u()));
 // Derive inventory variables, collection/use sets and visibility from the checked action log.
 for(auto action:history){
  if((action&0xc0000000U)==0x40000000U){if(!restored.use(action&0x3fffffffU))throw ParseError("invalid saved item use");}
  else if(action&0x80000000U){const auto id=action&0x7fffffffU;const auto rule=std::find_if(items_.graph_.combinations.begin(),items_.graph_.combinations.end(),[&](const Combination& c){return c.result==id;});if(rule==items_.graph_.combinations.end() || !restored.combine(rule->first,rule->second))throw ParseError("invalid saved combination");}
  else if(!restored.pickup(action))throw ParseError("invalid saved pickup");
 }
 if(restored.history_!=history)throw ParseError("story action history differs");
 if(cleared)restored.inventory_.clear();
 std::set<std::pair<std::uint32_t,std::uint32_t>> allowed,shown;
 for(const auto& l:setup_.layers)allowed.insert({l.object,l.presentation});
 for(auto id:{10030U,10002U,10003U,10004U,10001U,10005U,10018U,10020U,10031U})allowed.insert({id,0});
 for(auto id:{10006U,10008U,10009U})for(unsigned p=0;p<2;++p)allowed.insert({id,p});
 for(unsigned p=0;p<3;++p)allowed.insert({10014,p});
 const auto count=r.count(512);for(std::size_t i=0;i<count;++i){const auto id=static_cast<std::uint32_t>(r.u()),pres=static_cast<std::uint32_t>(r.u());if(!allowed.count({id,pres}) || !shown.insert({id,pres}).second)throw ParseError("invalid saved presentation");}
 auto enabled=r.flags(setup_.access.size());
 if(r.count(items_.graph_.nodes.size())!=items_.graph_.nodes.size())throw ParseError("saved channel map differs");
 std::map<std::uint32_t,std::vector<bool>> channels;
 for(std::size_t i=0;i<items_.graph_.nodes.size();++i){const auto id=static_cast<std::uint32_t>(r.u());const auto& node=scene_node(items_.graph_,id);if(channels.count(id))throw ParseError("duplicate saved node");channels[id]=r.flags(node.channels);}
 auto graph=items_.graph_;if(r.count(graph.exits.size())!=graph.exits.size())throw ParseError("saved exits differ");for(auto& e:graph.exits)e.enabled=r.flag();
 std::optional<StoryCue> playing;if(r.flag())playing=r.cue();
 std::deque<StoryCue> pending;const auto n=r.count(32);for(std::size_t i=0;i<n;++i)pending.push_back(r.cue());
 std::uint64_t previous=0;
 const auto valid_cue=[&](const StoryCue& c){
  if(!c.serial || c.serial>serial || c.serial<=previous)throw ParseError("invalid saved cue serial");
  previous=c.serial;
  if(c.kind==StoryCue::Node){scene_node(graph,c.id);if(!c.asset.empty())throw ParseError("invalid node cue asset");}
  else if(c.kind==StoryCue::Puzzle){if(!c.asset.empty() || std::none_of(setup_.backgrounds.begin(),setup_.backgrounds.end(),[&](const StageBackground& b){return b.id==c.id;}))throw ParseError("invalid puzzle cue");}
  else if(c.kind==StoryCue::Speech){if(!c.asset.empty() || std::none_of(setup_.dialogues.begin(),setup_.dialogues.end(),[&](const DialogueResource& d){return d.id==c.id;}))throw ParseError("invalid saved speech");}
  else if(c.kind==StoryCue::Movie){const std::vector<std::string> movies{"A01S01_AN001","A01S01_AN002","A01S01_AN003","A01S01_AN004","A01S01_AN006","A01S01_AN007A","A01S01_AN007B","A01S01_AN007C","A01S01_AN009"};if(std::find(movies.begin(),movies.end(),c.asset)==movies.end())throw ParseError("invalid saved movie");}
  else if(c.id!=10023 || c.asset!="First escape complete. The next conversation/topic puzzle is not implemented yet.")throw ParseError("invalid boundary cue");
 };
 if(playing){if(playing->kind!=StoryCue::Movie && playing->kind!=StoryCue::Speech)throw ParseError("invalid playing cue");valid_cue(*playing);}else if(resume.media_ms)throw ParseError("media offset without playback");
 for(const auto& c:pending)valid_cue(c);
 if(r.p!=r.b.size())throw ParseError("trailing story save data");
 if(resume.selected && !restored.owns(*resume.selected) && *resume.selected!=10010)throw ParseError("saved selected item is not owned");
 // Commit only after every record has been checked.
 items_=std::move(restored);graph_=std::move(graph);shown_=std::move(shown);enabled_=std::move(enabled);channels_=std::move(channels);
 serial_=serial;clock_=clock;deadline_=deadline;scene_=static_cast<std::uint32_t>(scene);phase_=static_cast<std::uint32_t>(phase);character_=static_cast<std::uint32_t>(character);deadline_kind_=static_cast<unsigned>(kind);
 puzzle_=puzzle;failed_=failed;boundary_=boundary;bag_cleared_=cleared;playing_.reset();pending_=std::move(pending);if(playing)pending_.push_front(*playing);
 return resume;
}
}
