#include "jerusalem/script_story.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace jerusalem {
namespace {
// Original dispatchers (aApplication virtuals); each selects the area handler itself.
constexpr std::uint32_t game_user_call=0x430940;  // (kind,value)
constexpr std::uint32_t use_event=0x42e3c0;       // (object,event,scene,in_puzzle,x,y)
constexpr std::uint32_t combine_event=0x42ee20;   // (held,target)
// Player movement, after the move: (new scene, previous scene, 0, event, kind), kind 0
// rotation->rotation (0042f030); 1 rotation->puzzle, 2 puzzle->rotation, 3 puzzle->puzzle (0042f2e0).
constexpr std::uint32_t rotation_move_event=0x42f030;
constexpr std::uint32_t puzzle_move_event=0x42f2e0;
constexpr std::uint32_t timer_event=0x42f530;     // (timer)
constexpr std::uint32_t sound_end_event=0x430450; // (sound,0,flags)
constexpr std::uint32_t box_event=0x435be0;       // (6,6,topic,0) = topic chosen
constexpr std::uint64_t handler_steps=5000000;
constexpr char save_magic[8]={'J','S','C','R','I','P','T','1'};

std::string upper(std::string s) { for (auto& c:s) if (c>='a'&&c<='z') c=char(c-'a'+'A'); return s; }

class Writer {
 public:
  std::vector<std::uint8_t> out;
  void u8(std::uint8_t v) { out.push_back(v); }
  void u32(std::uint32_t v) { for (int i=0;i<4;++i) out.push_back(std::uint8_t(v>>(8*i))); }
  void u64(std::uint64_t v) { u32(std::uint32_t(v)); u32(std::uint32_t(v>>32)); }
  void f64(double v) { std::uint64_t b; std::memcpy(&b,&v,8); u64(b); }
  void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); out.insert(out.end(),s.begin(),s.end()); }
};
class Reader {
  Bytes b_; std::size_t p_=0;
 public:
  explicit Reader(Bytes b):b_(b) {}
  std::uint8_t u8() { const auto v=b_.u8(p_); p_+=1; return v; }
  std::uint32_t u32() { const auto v=b_.u32(p_); p_+=4; return v; }
  std::uint64_t u64() { const std::uint64_t lo=u32(); return lo|(std::uint64_t(u32())<<32); }
  double f64() { const auto bits=u64(); double v; std::memcpy(&v,&bits,8); return v; }
  std::uint32_t count(std::uint32_t limit) { const auto n=u32(); if (n>limit) throw ParseError("script save count out of range"); return n; }
  std::string str() { const auto n=count(4096); std::string s; for (std::uint32_t i=0;i<n;++i) s+=char(u8()); return s; }
  bool done() const { return p_==b_.size(); }
};
}

TopicBook parse_topic_book(const std::string& text) {
  TopicBook book;
  std::istringstream in(text);
  std::string line;
  TopicBook::Section* current=nullptr;
  while (std::getline(in,line)) {
    if (!line.empty() && line.back()=='\r') line.pop_back();
    const auto first=line.find_first_not_of(" \t");
    if (first==std::string::npos) continue;
    std::istringstream fields(line.substr(first));
    if (line[first]=='#') {
      fields.get();
      std::uint32_t key=0,set=0;
      if (!(fields>>key>>set)) { current=nullptr; continue; }
      current=&book.sections[key];
      current->set=set; current->topics.clear();
    } else if (current) {
      std::uint32_t id=0;
      if (fields>>id) current->topics.push_back(id);
    }
  }
  return book;
}

ScriptStory::ScriptStory(std::vector<std::uint8_t> executable,TopicBook topics,std::uint32_t seed)
    :executable_(std::move(executable)),machine_(Bytes(executable_)),book_(std::move(topics)),random_(seed) {
  app_=machine_.scratch(0x1000);
  machine_.write32(0x4d0a90,app_);
  // The original registers every area at start-up.
  for (unsigned a=1;a<=10;++a) models_.emplace(a,record_area_setup(machine_,app_,a));
  for (auto& [a,m]:models_) {
    resolve_area_sounds(Bytes(executable_),m,models_);
    for (const auto& [id,v]:m.variables) variables_.emplace(id,Variable{v.size,v.value});
  }
  install_runtime();
  pristine_=machine_.image();
}

void ScriptStory::install_runtime() {
  for (const auto& api:engine_apis)
    machine_.trap(api.address,[this,&api](const X86Machine::Call& c){ return native(api,c); },api.args,api.callee_pops);
  for (const auto address:{0x42ac20U,0x42acd0U}) machine_.trap(address,[](const X86Machine::Call&){ return 1U; },8);  // drag cursors
}

void ScriptStory::push(StoryCue::Kind kind,std::uint32_t id,const std::string& asset,Camera camera) {
  pending_.push_back({kind,id,asset,camera,++serial_});
}

bool ScriptStory::dialogue(std::uint32_t id) const {
  for (const auto& [a,m]:models_) { const auto s=m.sounds.find(id); if (s!=m.sounds.end()) return s->second.category==5; }
  return false;
}

void ScriptStory::select_area(unsigned area) {
  if (!models_.count(area)) throw ParseError("script selects unknown area "+std::to_string(area));
  area_=area;
  const auto& m=models_.at(area);
  setup_=m.stage; graph_=m.graph; puzzle_exits_=m.puzzle_exits;
  for (const auto& n:graph_.nodes) channels_.emplace(n.id,std::vector<bool>(n.channels,false));
  for (const auto& a:setup_.access) access_.emplace(std::pair{a.object,a.index},a.enabled);
  for (const auto& e:graph_.exits) exits_.emplace(std::tuple{false,e.source,e.slot},e.enabled);
  for (const auto& e:puzzle_exits_) exits_.emplace(std::tuple{true,e.source,e.slot},e.enabled);
  if (activated_.insert(area).second)
    for (const auto& call:m.runtime_calls)
      if (const auto* api=find_engine_api(call.target)) native(*api,{call.target,app_,call.args});
  rebuild_graph();
}

void ScriptStory::rebuild_graph() {
  visible_graph_=graph_;
  visible_graph_.exits.clear();
  for (const auto& e:graph_.exits) if (exits_.at({false,e.source,e.slot})) visible_graph_.exits.push_back(e);
}

void ScriptStory::dispatch(std::uint32_t function,std::vector<std::uint32_t> args) {
  if (dispatching_) { deferred_.push_back({function,std::move(args)}); return; }
  deferred_.push_back({function,std::move(args)});
  dispatching_=true;
  try {
    while (!deferred_.empty()) {
      auto next=std::move(deferred_.front()); deferred_.pop_front();
      machine_.call(next.function,app_,next.args,handler_steps);
    }
  } catch (...) { dispatching_=false; deferred_.clear(); throw; }
  dispatching_=false;
}

void ScriptStory::start(std::uint32_t kind,std::uint32_t value) {
  dispatch(game_user_call,{kind,value});
  if (!area_) throw ParseError("the original entry did not select an area");
}

void ScriptStory::show(std::uint32_t object,std::uint32_t presentation,bool on) {
  if (on) shown_.insert({object,presentation}); else shown_.erase({object,presentation});
  for (const auto& [a,m]:models_)
    for (const auto& l:m.stage.layers)
      if (l.object==object && l.presentation==presentation && l.panorama) {
        auto& channels=channels_[l.scene];
        if (channels.size()<=l.channel) channels.resize(l.channel+1,false);
        channels[l.channel]=on;
      }
}

void ScriptStory::show_all(std::uint32_t object,bool on) {
  std::set<std::uint32_t> presentations;
  for (const auto& [a,m]:models_) for (const auto& l:m.stage.layers) if (l.object==object) presentations.insert(l.presentation);
  for (const auto p:presentations) show(object,p,on);
}

void ScriptStory::set_access(std::uint32_t object,std::optional<std::pair<std::uint32_t,std::uint32_t>> range,bool on) {
  for (auto& [key,enabled]:access_)
    if (key.first==object && (!range || (key.second>=range->first && key.second<=range->second))) enabled=on;
}

std::uint32_t ScriptStory::native(const EngineApi& api,const X86Machine::Call& c) {
  const auto& a=c.args;
  const std::string_view n=api.name;
  if (trail_.size()>=64) trail_.pop_front();
  {
    std::string t(n); t+='(';
    for (std::size_t i=0;i<a.size();++i) t+=(i?",":"")+std::to_string(std::int32_t(a[i]));
    trail_.push_back(t+")");
  }
  if (const auto v=variable_api(api.address)) {
    auto found=variables_.find(a[0]);
    if (v->kind==0) { variables_.emplace(a[0],Variable{v->size,a[1]}); return 1; }
    if (found==variables_.end()) return 0;  // aVar logs and fails
    if (v->kind==1) {
      found->second.value=found->second.size==1?(a[1]&0xff):found->second.size==2?(a[1]&0xffff):a[1];
      return 1;
    }
    return found->second.value;
  }
  if (n=="Log"||n=="Log2"||n=="SetMode5F"||n=="FadOut"||n=="Refresh"||n=="RotSetAlp"||n=="SouSetVol"||n=="SouStop"||
      n=="PuzModOn"||n=="PuzModOff"||n=="ClearFlag6B"||n=="EncSetMode"||n=="EncStep") return 1;
  if (n=="EncRowBefore"||n=="EncRowAt"||n=="EncRowAfter"||n=="EncColBefore"||n=="EncColAt"||n=="EncColAfter") return 0;
  if (n=="GameCall") { deferred_.push_back({game_user_call,{a[0],a[1]}}); return 1; }
  if (n=="SetSpaceSoft"||n=="SetSpace") { if (a[0]>=1 && a[0]<=10) select_area(a[0]); return 1; }
  if (n=="ImgTransition") return 1;
  if (n=="SouIsPlaying") return playing_ && playing_->kind==StoryCue::Speech && a[0]==5?1:0;
  if (n=="SouAnyPlaying") return playing_?1:0;
  if (n=="GetSpace") return area_;
  if (n=="PlayMovie") { push(StoryCue::Movie,0,upper(machine_.read_string(a[0],64))); return 1; }
  if (n=="Credits") { push(StoryCue::Boundary,area_,"The original game ends here."); return 1; }
  if (n=="GoPuz") { logical_scene_=a[0]; logical_puzzle_=true; push(StoryCue::Puzzle,a[0]); return 1; }
  if (n=="RotSetAct") { logical_scene_=rotation_=a[0]; logical_puzzle_=false; push(StoryCue::Node,a[0]); return 1; }
  if (n=="InRotation") return logical_puzzle_?0:1;
  if (n=="CurPuz") return logical_puzzle_?logical_scene_:0;
  if (n=="CurRot") return rotation_;
  if (n=="ObjAccOn"||n=="ObjAccOff") { set_access(a[0],{},n=="ObjAccOn"); return 1; }
  if (n=="ObjAccRangeOn"||n=="ObjAccRangeOff") { set_access(a[0],std::pair{a[1],a[2]},n=="ObjAccRangeOn"); return 1; }
  if (n=="ObjPrePauAni"||n=="ObjPrePauTimAni") { paused_.insert({a[0],a[1]}); if (n=="ObjPrePauTimAni") paused_.erase({a[0],a[1]}); return 1; }
  if (n=="ObjPreUnPauAni") { paused_.erase({a[0],a[1]}); return 1; }
  if (n=="ObjPreSho") { show(a[0],a[1],true); return 1; }
  if (n=="ObjPreHid") { show(a[0],a[1],false); return 1; }
  if (n=="ObjPreHidDeaPuz") { show(a[0],a[1],false); set_access(a[0],{},false); return 1; }
  if (n=="ObjShoAll"||n=="ObjHidAll") { show_all(a[0],n=="ObjShoAll"); return 1; }
  if (n=="ObjPreGetX"||n=="ObjPreGetY") {
    unsigned index=0;
    for (const auto& [ar,m]:models_) for (const auto& l:m.stage.layers)
      if (l.object==a[0] && l.presentation==a[1] && index++==a[2]) return std::uint32_t(n=="ObjPreGetX"?l.x:l.y);
    return 0;
  }
  if (n=="RotConRangeOn"||n=="RotConRangeOff") {
    for (auto& [key,enabled]:exits_) if (!std::get<0>(key) && std::get<1>(key)==a[0] && std::get<2>(key)>=a[1] && std::get<2>(key)<=a[2]) enabled=n=="RotConRangeOn";
    rebuild_graph(); return 1;
  }
  if (n=="BagAdd") { if (std::find(bag_.begin(),bag_.end(),a[0])==bag_.end()) bag_.push_back(a[0]); return 1; }
  if (n=="BagRem") { bag_.erase(std::remove(bag_.begin(),bag_.end(),a[0]),bag_.end()); if (held_==a[0]) held_.reset(); return 1; }
  if (n=="BagRemAll") { bag_.clear(); held_.reset(); return 1; }
  if (n=="BagIsIn") return std::find(bag_.begin(),bag_.end(),a[0])!=bag_.end()?1:0;
  if (n=="HasHeld") return held_?1:0;
  if (n=="HeldItem") return held_.value_or(0);
  if (n=="ReleaseHeld") { held_.reset(); return 1; }
  if (n=="TimSta") { timers_[a[0]]=clock_+a[1]; return 1; }
  if (n=="TimSto") { timers_.erase(a[0]); return 1; }
  if (n=="TimStoAll") { timers_.clear(); return 1; }
  if (n=="TimIsOn") return timers_.count(a[0])?1:0;
  if (n=="Speak") { if (dialogue(a[0])) push(StoryCue::Speech,a[0]); return 1; }
  if (n=="BoxShow") {
    if (a[0]==6) {
      box_open_=true; box_set_=a[2];
      if (!topic_lists_.count(box_set_)) {
        const auto s=book_.sections.find(box_set_);
        topic_lists_[box_set_]=s!=book_.sections.end()?s->second.topics:std::vector<std::uint32_t>{};
      }
    }
    return 1;
  }
  if (n=="BoxHide") { if (a[0]==6) box_open_=false; return 1; }
  if (n=="rand") { random_=random_*214013U+2531011U; return (random_>>16)&0x7fff; }
  throw ParseError("unimplemented engine function "+std::string(n));
}

std::optional<StoryCue> ScriptStory::next() {
  if (playing_ || pending_.empty()) return {};
  auto cue=pending_.front(); pending_.pop_front();
  if (cue.kind==StoryCue::Node) { scene_=cue.id; puzzle_=false; }
  else if (cue.kind==StoryCue::Puzzle) { scene_=cue.id; puzzle_=true; }
  else if (cue.kind==StoryCue::Boundary) boundary_=true;
  if (cue.kind==StoryCue::Speech || cue.kind==StoryCue::Movie) playing_=cue;
  return cue;
}

bool ScriptStory::complete(std::uint64_t serial) {
  if (!playing_ || playing_->serial!=serial) return false;
  const auto cue=*playing_; playing_.reset();
  if (cue.kind==StoryCue::Speech) dispatch(sound_end_event,{cue.id,0,0});
  return true;
}

void ScriptStory::tick(std::uint64_t elapsed) {
  clock_+=elapsed;
  for (;;) {
    std::optional<std::uint32_t> due;
    for (const auto& [id,at]:timers_) if (at<=clock_ && (!due || at<timers_.at(*due))) due=id;
    if (!due) break;
    timers_.erase(*due);
    dispatch(timer_event,{*due});
  }
}

const Exit* ScriptStory::find_exit(std::uint32_t source,std::uint32_t target) const {
  for (const auto& e:visible_graph_.exits) if (e.source==source && e.target==target) return &e;
  return nullptr;
}

bool ScriptStory::enter_node(std::uint32_t id,Camera camera) {
  if (busy() || boundary_) return false;
  const auto* exit=find_exit(scene_,id);
  if (!exit || exit->puzzle) return false;
  const auto source=scene_,slot=exit->slot,event=exit->event;
  logical_scene_=rotation_=id; logical_puzzle_=false;
  push(StoryCue::Node,id,{},camera);
  (void)slot;
  dispatch(rotation_move_event,{id,source,0,event,0});
  return true;
}

bool ScriptStory::enter_puzzle(std::uint32_t id) {
  if (busy() || boundary_) return false;
  const auto* exit=find_exit(scene_,id);
  if (!exit || !exit->puzzle) return false;
  const auto source=scene_,slot=exit->slot,event=exit->event;
  logical_scene_=id; logical_puzzle_=true;
  push(StoryCue::Puzzle,id,{},exit->arrival);
  (void)slot;
  dispatch(puzzle_move_event,{id,source,0,event,1});
  return true;
}

bool ScriptStory::activate(std::uint32_t object,std::uint32_t event,std::optional<std::uint32_t> item) {
  if (busy() || boundary_) return false;
  if ((object&0xff000000U)==exit_tag) {
    const auto index=object&0xffffffU;
    if (index>=puzzle_exits_.size() || !puzzle_) return false;
    const auto e=puzzle_exits_[index];
    if (e.source!=scene_ || !exits_.at({true,e.source,e.slot})) return false;
    logical_scene_=e.target; logical_puzzle_=e.puzzle;
    if (e.puzzle) push(StoryCue::Puzzle,e.target); else { rotation_=e.target; push(StoryCue::Node,e.target,{},e.arrival); }
    dispatch(puzzle_move_event,{e.target,e.source,0,e.event,e.puzzle?3U:2U});
    return true;
  }
  held_=item;
  try { dispatch(use_event,{object,event,scene_,puzzle_?1U:0U,0,0}); } catch (...) { held_.reset(); throw; }
  held_.reset();
  return true;
}

std::optional<std::uint32_t> ScriptStory::combine(std::uint32_t first,std::uint32_t second) {
  if (busy() || boundary_) return {};
  const auto before=bag_;
  held_=first;
  try { dispatch(combine_event,{first,second}); } catch (...) { held_.reset(); throw; }
  held_.reset();
  for (const auto item:bag_) if (std::find(before.begin(),before.end(),item)==before.end()) return item;
  return {};
}

std::vector<std::uint32_t> ScriptStory::topics() const {
  if (!box_open_ || busy()) return {};
  const auto found=topic_lists_.find(box_set_);
  return found==topic_lists_.end()?std::vector<std::uint32_t>{}:found->second;
}

bool ScriptStory::choose_topic(std::uint32_t id) {
  const auto available=topics();
  if (std::find(available.begin(),available.end(),id)==available.end()) return false;
  auto& list=topic_lists_[box_set_];
  list.erase(std::find(list.begin(),list.end(),id));
  if (const auto s=book_.sections.find(id); s!=book_.sections.end() && s->first!=s->second.set) {
    auto& target=topic_lists_[s->second.set];
    for (const auto t:s->second.topics) if (std::find(target.begin(),target.end(),t)==target.end()) target.push_back(t);
  }
  dispatch(box_event,{6,6,id,0});
  if (dialogue(id)) push(StoryCue::Speech,id);
  return true;
}

std::string ScriptStory::item_name(std::uint32_t id) const {
  for (const auto& [a,m]:models_) for (const auto& o:m.stage.objects) if (o.id==id) return o.name;
  return "Item "+std::to_string(id);
}

std::vector<StageAccess> ScriptStory::hotspots() const {
  std::vector<StageAccess> result;
  if (busy() || boundary_) return result;
  for (const auto& a:setup_.access)
    if (a.scene==scene_ && a.panorama!=puzzle_ && access_.at({a.object,a.index})) result.push_back(a);
  if (puzzle_)
    for (std::size_t i=0;i<puzzle_exits_.size();++i) {
      const auto& e=puzzle_exits_[i];
      if (e.source==scene_ && exits_.at({true,e.source,e.slot}))
        result.push_back({exit_tag|static_cast<std::uint32_t>(i),e.source,e.slot,false,true,e.left,e.top,e.right,e.bottom,e.cursor,e.event});
    }
  return result;
}

std::optional<StageAccess> ScriptStory::hit(int x,int y) const {
  for (const auto& a:hotspots()) if (x>=a.left && x<a.right && y>=a.top && y<a.bottom) return a;
  return {};
}

std::vector<StageLayer> ScriptStory::layers() const {
  std::vector<StageLayer> result;
  for (const auto& l:setup_.layers) {
    if (l.scene!=scene_ || l.panorama==puzzle_ || !shown_.count({l.object,l.presentation})) continue;
    if (l.panorama) { const auto c=channels_.find(l.scene); if (c==channels_.end() || l.channel>=c->second.size() || !c->second[l.channel]) continue; }
    result.push_back(l);
  }
  std::stable_sort(result.begin(),result.end(),[](const StageLayer& a,const StageLayer& b){ return a.order>b.order; });
  return result;
}

std::vector<bool> ScriptStory::visibility(std::uint32_t node) const {
  const auto c=channels_.find(node);
  return c==channels_.end()?std::vector<bool>{}:c->second;
}

void ScriptStory::clear_speech_animation(const Dan& d) {
  for (const auto& level:d.levels)
    if (std::any_of(setup_.layers.begin(),setup_.layers.end(),[&](const StageLayer& l){ return l.object==level.second && l.presentation==level.third; }))
      show(level.second,level.third,false);
}

void ScriptStory::speech_animation(const Dan& d,std::uint64_t time) {
  if (!playing_ || playing_->kind!=StoryCue::Speech) return;
  std::optional<std::pair<std::uint32_t,std::uint32_t>> active;
  for (const auto& range:d.intervals) if (range.first<=time && time<=range.second) {
    for (const auto& level:d.levels) if (range.third && level.first==range.third) { active={{level.second,level.third}}; break; }
    break;
  }
  for (const auto& level:d.levels) {
    if (!std::any_of(setup_.layers.begin(),setup_.layers.end(),[&](const StageLayer& l){ return l.object==level.second && l.presentation==level.third; })) continue;
    const std::pair<std::uint32_t,std::uint32_t> key{level.second,level.third};
    const bool on=active && *active==key;
    if (bool(shown_.count(key))!=on) show(level.second,level.third,on);
  }
}

std::vector<std::uint8_t> ScriptStory::save(const StoryResume& resume,std::uint64_t assets) const {
  if (dispatching_) throw ParseError("cannot save while a handler runs");
  Writer w;
  for (const auto c:save_magic) w.u8(std::uint8_t(c));
  w.u64(assets);
  w.f64(resume.camera.yaw); w.f64(resume.camera.pitch); w.f64(resume.camera.fov);
  w.u64(resume.media_ms); w.u64(resume.visual_ms); w.u8(resume.selected?1:0); w.u32(resume.selected.value_or(0));
  w.u32(area_); w.u32(scene_); w.u32(logical_scene_); w.u32(rotation_);
  w.u8(puzzle_); w.u8(logical_puzzle_); w.u8(boundary_); w.u8(box_open_); w.u32(box_set_);
  w.u32(random_); w.u64(clock_); w.u64(serial_);
  w.u32(static_cast<std::uint32_t>(activated_.size())); for (const auto a:activated_) w.u32(a);
  for (const auto* set:{&shown_,&paused_}) { w.u32(static_cast<std::uint32_t>(set->size())); for (const auto& [o,p]:*set) { w.u32(o); w.u32(p); } }
  w.u32(static_cast<std::uint32_t>(access_.size())); for (const auto& [k,v]:access_) { w.u32(k.first); w.u32(k.second); w.u8(v); }
  w.u32(static_cast<std::uint32_t>(exits_.size())); for (const auto& [k,v]:exits_) { w.u8(std::get<0>(k)); w.u32(std::get<1>(k)); w.u32(std::get<2>(k)); w.u8(v); }
  w.u32(static_cast<std::uint32_t>(channels_.size())); for (const auto& [node,bits]:channels_) { w.u32(node); w.u32(static_cast<std::uint32_t>(bits.size())); for (const bool b:bits) w.u8(b); }
  w.u32(static_cast<std::uint32_t>(variables_.size())); for (const auto& [id,v]:variables_) { w.u32(id); w.u8(std::uint8_t(v.size)); w.u32(v.value); }
  w.u32(static_cast<std::uint32_t>(bag_.size())); for (const auto i:bag_) w.u32(i);
  w.u32(static_cast<std::uint32_t>(timers_.size())); for (const auto& [id,at]:timers_) { w.u32(id); w.u64(at); }
  w.u32(static_cast<std::uint32_t>(topic_lists_.size()));
  for (const auto& [set,list]:topic_lists_) { w.u32(set); w.u32(static_cast<std::uint32_t>(list.size())); for (const auto t:list) w.u32(t); }
  // Active media is requeued in front of the pending cues.
  std::vector<StoryCue> cues;
  if (playing_) cues.push_back(*playing_);
  cues.insert(cues.end(),pending_.begin(),pending_.end());
  w.u32(static_cast<std::uint32_t>(cues.size()));
  for (const auto& c:cues) { w.u8(std::uint8_t(c.kind)); w.u32(c.id); w.str(c.asset); w.f64(c.camera.yaw); w.f64(c.camera.pitch); w.f64(c.camera.fov); }
  const auto& image=machine_.image();
  std::vector<std::uint32_t> changed;
  for (std::uint32_t i=0;i<image.size();++i) if (image[i]!=pristine_[i]) changed.push_back(i);
  w.u32(static_cast<std::uint32_t>(changed.size())); for (const auto i:changed) { w.u32(i); w.u8(image[i]); }
  return std::move(w.out);
}

StoryResume ScriptStory::load(Bytes bytes,std::uint64_t assets) {
  if (bytes.size()>(1U<<22)) throw ParseError("script save too large");
  Reader r(bytes);
  for (const auto c:save_magic) if (r.u8()!=std::uint8_t(c)) throw ParseError("not a script story save");
  if (r.u64()!=assets) throw ParseError("script save belongs to different game files");
  ScriptStory s(executable_,book_,1);
  StoryResume resume;
  resume.camera.yaw=r.f64(); resume.camera.pitch=r.f64(); resume.camera.fov=r.f64();
  resume.media_ms=r.u64(); resume.visual_ms=r.u64();
  const bool selected=r.u8(); const auto item=r.u32(); if (selected) resume.selected=item;
  const auto area=r.u32();
  s.scene_=r.u32(); s.logical_scene_=r.u32(); s.rotation_=r.u32();
  s.puzzle_=r.u8(); s.logical_puzzle_=r.u8(); s.boundary_=r.u8(); s.box_open_=r.u8(); s.box_set_=r.u32();
  s.random_=r.u32(); s.clock_=r.u64(); s.serial_=r.u64();
  for (auto n=r.count(16);n--;) { const auto a=r.u32(); if (!s.models_.count(a)) throw ParseError("script save names an unknown area"); s.activated_.insert(a); }
  if (!s.activated_.count(area)) throw ParseError("script save area was never entered");
  // Select without replaying setup calls (the saved state already includes them).
  s.select_area(area);
  for (auto* set:{&s.shown_,&s.paused_}) { set->clear(); for (auto n=r.count(1U<<16);n--;) { const auto o=r.u32(); set->insert({o,r.u32()}); } }
  s.access_.clear(); for (auto n=r.count(1U<<16);n--;) { const auto o=r.u32(); const auto i=r.u32(); s.access_[{o,i}]=r.u8()!=0; }
  s.exits_.clear(); for (auto n=r.count(1U<<16);n--;) { const bool p=r.u8(); const auto src=r.u32(); const auto slot=r.u32(); s.exits_[{p,src,slot}]=r.u8()!=0; }
  s.channels_.clear(); for (auto n=r.count(1U<<12);n--;) { const auto node=r.u32(); auto& bits=s.channels_[node]; for (auto k=r.count(256);k--;) bits.push_back(r.u8()!=0); }
  s.variables_.clear(); for (auto n=r.count(1U<<16);n--;) { const auto id=r.u32(); const unsigned size=r.u8(); s.variables_[id]={size,r.u32()}; }
  for (auto n=r.count(4096);n--;) s.bag_.push_back(r.u32());
  for (auto n=r.count(4096);n--;) { const auto id=r.u32(); s.timers_[id]=r.u64(); }
  for (auto n=r.count(4096);n--;) { const auto set=r.u32(); auto& list=s.topic_lists_[set]; for (auto k=r.count(4096);k--;) list.push_back(r.u32()); }
  for (auto n=r.count(4096);n--;) {
    StoryCue c; c.kind=static_cast<StoryCue::Kind>(r.u8()); if (c.kind>StoryCue::Boundary) throw ParseError("bad script save cue");
    c.id=r.u32(); c.asset=r.str(); c.camera.yaw=r.f64(); c.camera.pitch=r.f64(); c.camera.fov=r.f64(); c.serial=++s.serial_;
    s.pending_.push_back(c);
  }
  auto image=s.machine_.image();
  for (auto n=r.count(static_cast<std::uint32_t>(image.size()));n--;) { const auto i=r.u32(); if (i>=image.size()) throw ParseError("bad script save image offset"); image[i]=r.u8(); }
  if (!r.done()) throw ParseError("trailing bytes in script save");
  s.machine_.restore_image(image);
  s.rebuild_graph();
  // Commit: adopt the loaded state wholesale (the machine's traps refer to `this`).
  std::swap(machine_,s.machine_);
  install_runtime();
  area_=s.area_; setup_=std::move(s.setup_); graph_=std::move(s.graph_); visible_graph_=std::move(s.visible_graph_); puzzle_exits_=std::move(s.puzzle_exits_);
  activated_=std::move(s.activated_); shown_=std::move(s.shown_); paused_=std::move(s.paused_); access_=std::move(s.access_); exits_=std::move(s.exits_);
  channels_=std::move(s.channels_); variables_=std::move(s.variables_); bag_=std::move(s.bag_); held_.reset(); timers_=std::move(s.timers_);
  box_open_=s.box_open_; box_set_=s.box_set_; topic_lists_=std::move(s.topic_lists_);
  scene_=s.scene_; logical_scene_=s.logical_scene_; rotation_=s.rotation_; puzzle_=s.puzzle_; logical_puzzle_=s.logical_puzzle_; boundary_=s.boundary_;
  random_=s.random_; clock_=s.clock_; serial_=s.serial_; pending_=std::move(s.pending_); playing_.reset(); deferred_.clear(); dispatching_=false;
  return resume;
}
}  // namespace jerusalem
