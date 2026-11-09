#pragma once
#include "jerusalem/engine_api.hpp"
#include "jerusalem/script_area.hpp"
#include "jerusalem/stage.hpp"
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace jerusalem {
// Topic lists from KEYWORDS.INI: "#key<TAB>set" headers followed by "id<TAB>text[<TAB>flag]".
// The section whose key equals its set is the set's initial list; the section keyed by a
// topic ID holds the follow-up topics added to that set when the topic is chosen
// (original box selection 0048c940).
struct TopicBook {
  struct Section { std::uint32_t set; std::vector<std::uint32_t> topics; };
  std::map<std::uint32_t,Section> sections;
};
TopicBook parse_topic_book(const std::string& text);

// Story for A03 onward. All area registrations come from running the original setup
// routines, and every story decision is made by the original area handlers, executed
// by X86Machine. The engine functions those handlers call are implemented natively.
class ScriptStory : public Story {
 public:
  ScriptStory(std::vector<std::uint8_t> executable,TopicBook topics,std::uint32_t seed=1);
  // Runs the original GameCall(kind,value) entry, e.g. (3,0) for the A03 start.
  void start(std::uint32_t kind=3,std::uint32_t value=0);
  StoryResume load(Bytes bytes,std::uint64_t assets);
  std::vector<std::uint8_t> save(const StoryResume& resume,std::uint64_t assets) const override;
  const std::optional<StoryCue>& playing() const override { return playing_; }
  std::optional<StoryCue> next() override;
  bool complete(std::uint64_t serial) override;
  void tick(std::uint64_t elapsed_ms) override;
  bool enter_node(std::uint32_t id,Camera camera={}) override;
  bool enter_puzzle(std::uint32_t id) override;
  bool activate(std::uint32_t object,std::uint32_t event,std::optional<std::uint32_t> item={}) override;
  bool pickup(std::uint32_t object) override { return activate(object,0); }
  std::optional<std::uint32_t> combine(std::uint32_t first,std::uint32_t second) override;
  std::vector<std::uint32_t> topics() const override;
  bool choose_topic(std::uint32_t id) override;
  std::string item_name(std::uint32_t id) const override;
  void speech_animation(const Dan& animation,std::uint64_t elapsed_ms) override;
  void clear_speech_animation(const Dan& animation) override;
  std::optional<StageAccess> hit(int x,int y) const override;
  std::vector<StageAccess> hotspots() const override;
  std::vector<StageLayer> layers() const override;
  std::vector<bool> visibility(std::uint32_t node) const override;
  std::optional<std::uint32_t> local_item() const override { return std::nullopt; }
  const SceneGraph& graph() const override { return visible_graph_; }
  const StageSetup& setup() const override { return setup_; }
  std::vector<std::uint32_t> inventory() const override { return bag_; }
  bool busy() const override { return playing_.has_value() || !pending_.empty(); }
  bool puzzle() const override { return puzzle_; }
  bool failed() const override { return false; }
  bool boundary() const override { return boundary_; }
  bool finished() const override { return boundary_; }
  std::uint32_t scene() const override { return scene_; }
  unsigned area() const override { return area_; }
  std::uint64_t remaining_ms() const override { return 0; }
  const AreaModel& model(unsigned area) const { return models_.at(area); }
  // Debug trail of engine calls made by handlers (bounded).
  const std::deque<std::string>& trail() const { return trail_; }
  // Puzzle exits are offered as accesses with this object-ID tag plus the exit index.
  static constexpr std::uint32_t exit_tag=0xfe000000;

 private:
  struct Dispatch { std::uint32_t function; std::vector<std::uint32_t> args; };
  std::vector<std::uint8_t> executable_;
  X86Machine machine_;
  std::uint32_t app_=0;
  std::map<unsigned,AreaModel> models_;
  TopicBook book_;
  std::vector<std::uint8_t> pristine_;
  // Current area view.
  unsigned area_=0;
  StageSetup setup_;
  SceneGraph graph_,visible_graph_;
  std::vector<Exit> puzzle_exits_;
  std::set<unsigned> activated_;
  // World state.
  std::set<std::pair<std::uint32_t,std::uint32_t>> shown_,paused_;
  std::map<std::pair<std::uint32_t,std::uint32_t>,bool> access_;  // (object,index) -> enabled
  std::map<std::tuple<bool,std::uint32_t,std::uint32_t>,bool> exits_;  // (from puzzle,source,slot)
  std::map<std::uint32_t,std::vector<bool>> channels_;
  struct Variable { unsigned size; std::uint32_t value; };
  std::map<std::uint32_t,Variable> variables_;
  std::vector<std::uint32_t> bag_;
  std::optional<std::uint32_t> held_;
  std::map<std::uint32_t,std::uint64_t> timers_;
  bool box_open_=false;
  std::uint32_t box_set_=0;
  std::map<std::uint32_t,std::vector<std::uint32_t>> topic_lists_;
  std::uint32_t scene_=0,logical_scene_=0,rotation_=0;
  bool puzzle_=false,logical_puzzle_=false,boundary_=false;
  std::uint32_t random_=1;
  std::uint64_t clock_=0,serial_=0;
  std::deque<StoryCue> pending_;
  std::optional<StoryCue> playing_;
  std::deque<Dispatch> deferred_;
  bool dispatching_=false;
  std::deque<std::string> trail_;

  void install_runtime();
  std::uint32_t native(const EngineApi& api,const X86Machine::Call& call);
  void dispatch(std::uint32_t function,std::vector<std::uint32_t> args);
  void select_area(unsigned area);
  void rebuild_graph();
  void push(StoryCue::Kind kind,std::uint32_t id=0,const std::string& asset={},Camera camera={});
  void show(std::uint32_t object,std::uint32_t presentation,bool visible);
  void show_all(std::uint32_t object,bool visible);
  void set_access(std::uint32_t object,std::optional<std::pair<std::uint32_t,std::uint32_t>> range,bool on);
  bool dialogue(std::uint32_t id) const;
  const Exit* find_exit(std::uint32_t source,std::uint32_t target) const;
};
}  // namespace jerusalem
