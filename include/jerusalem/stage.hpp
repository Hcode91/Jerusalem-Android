#pragma once
#include "jerusalem/navigation.hpp"
#include "jerusalem/interactions.hpp"
#include <deque>
#include <set>
namespace jerusalem {
struct StageBackground {std::uint32_t id;std::string image;int x,y;};
struct StageAccess {std::uint32_t object,scene,index;bool panorama,enabled;int left,top,right,bottom;std::uint32_t cursor,event;};
struct StageLayer {
  std::uint32_t object,presentation,scene;bool panorama,animated;
  // channel is a rotation channel, or the puzzle resource type (0 BMP / 1 TGA).
  std::string image;std::uint32_t channel,frames,mode,order;int x,y;double fps;
};
struct StageObject {std::uint32_t id;std::string name,icon;};
struct StageSetup {std::vector<StageBackground> backgrounds;std::vector<StageAccess> access;std::vector<StageLayer> layers;std::vector<DialogueResource> dialogues;std::vector<StageObject> objects;std::vector<SceneAmbient> ambient;};
StageSetup extract_a01_stage(Bytes executable);
StageSetup extract_a02_stage(Bytes executable);
// A01 followed by A02 records (stage text V3). IDs of the two areas are disjoint.
StageSetup extract_story_stage(Bytes executable);
// True when the model carries the A02 registrations.
bool stage_has_a02(const StageSetup& setup);
// The A01-only subset of a combined model, as written before A02 support.
StageSetup a01_stage_subset(const StageSetup& setup);
std::string write_stage(const StageSetup& setup);
StageSetup parse_stage(const std::string& text);
struct StoryAssetTags {std::uint64_t current,legacy;};
// Full-area validation and bounded content fingerprinting, shared with host tests.
StoryAssetTags a01_story_assets(const SceneGraph& graph,const StageSetup& setup,const std::filesystem::path& root);
// Presence/parse checks for the files A02 needs. A02 files are not part of the save fingerprint.
void validate_a02_assets(const StageSetup& setup,const std::filesystem::path& root);
// Only path components under root/data or root; rejects ambiguous casing/symlinks.
std::filesystem::path game_file(const std::filesystem::path& root,const std::vector<std::string>& components);
class StageArt {
  std::vector<std::uint8_t> data_;
  ArtDirectory directory_;
 public:
  explicit StageArt(std::vector<std::uint8_t> archive);
  Bitmap image(const std::string& basename) const;
};
// Fixed 640x480 original puzzle coordinates. Pixel bounds are half-open.
void stage_blit(Bitmap& destination,const Bitmap& source,int x,int y);
// Verified original PreAlpha/CopyAlpha integer arithmetic for TGA-named CNM.
void stage_blit_tga(Bitmap& destination,const Bitmap& source,int x,int y);
std::optional<HotspotPoint> stage_point(double x,double y,int viewport_width,int viewport_height);
std::size_t stage_frame(std::uint64_t elapsed,const StageLayer& layer);
// Independently authored A01 controller. No original machine code executes.
// Media completion must be acknowledged using its serial; cancelled/stale completions do nothing.
struct StoryCue {enum Kind {Node,Puzzle,Movie,Speech,Boundary} kind;std::uint32_t id=0;std::string asset;Camera camera{};std::uint64_t serial=0;};
struct StoryResume {Camera camera{};std::uint64_t media_ms=0,visual_ms=0;std::optional<std::uint32_t> selected;};
class A01Story {
  StageSetup setup_;SceneGraph graph_;InteractionState& items_;
  std::set<std::pair<std::uint32_t,std::uint32_t>> shown_;
  std::vector<bool> enabled_;
  std::map<std::uint32_t,std::vector<bool>> channels_;
  std::deque<StoryCue> pending_;
  std::optional<StoryCue> playing_;
  std::uint64_t serial_=0,deadline_=0,clock_=0;
  std::uint32_t scene_=0,phase_=0,character_=0;
  unsigned deadline_kind_=1;
  bool puzzle_=false,failed_=false,boundary_=false,bag_cleared_=false;
  bool camp_=false,cards_given_=false,jug_=false,finished_=false;
  unsigned topics_=0,bowl_=0,cards_used_=0;
  // A02 (verified handlers 0044efd0 entry, 0044a180 use, 0044a3f0 speech end, 0044a5d0 topic).
  unsigned area_=1,a02_topics_=0;
  void enter_a02();
  std::uint32_t random_=1;
  void push(StoryCue::Kind kind,std::uint32_t id=0,const std::string& asset={},Camera camera={});
  void show(std::uint32_t object,std::uint32_t presentation,bool visible);
  void all(std::uint32_t object,bool visible);
  void enable(std::uint32_t object,bool on);
  void access(std::uint32_t object,std::uint32_t index,bool on);
  void character(std::uint32_t object);
  void bag_add(std::uint32_t item);
  void bag_remove(std::uint32_t item);
  unsigned random_choice(unsigned count);
 public:
  A01Story(StageSetup setup,SceneGraph graph,InteractionState& inventory,std::uint32_t seed=1);
  void start();
  std::vector<std::uint8_t> save(const StoryResume& resume,std::uint64_t assets) const;
  // Validates before replacing state; requeues active media without completing it.
  StoryResume load(Bytes bytes,std::uint64_t assets,std::optional<std::uint64_t> legacy_assets={});
  const std::optional<StoryCue>& playing() const {return playing_;}
  std::optional<StoryCue> next();
  bool complete(std::uint64_t serial);
  void tick(std::uint64_t elapsed_ms);
  bool enter_node(std::uint32_t id,Camera camera={});
  bool enter_puzzle(std::uint32_t id);
  bool activate(std::uint32_t object,std::uint32_t event,std::optional<std::uint32_t> item={});
  bool pickup(std::uint32_t object);
  std::optional<std::uint32_t> combine(std::uint32_t first,std::uint32_t second);
  std::vector<std::uint32_t> topics() const;
  bool choose_topic(std::uint32_t id);
  std::string item_name(std::uint32_t id) const;
  void speech_animation(const Dan& animation,std::uint64_t elapsed_ms);
  void clear_speech_animation(const Dan& animation);
  std::optional<StageAccess> hit(int x,int y) const;
  std::vector<StageAccess> hotspots() const;
  std::vector<StageLayer> layers() const;
  std::vector<bool> visibility(std::uint32_t node) const;
  std::optional<std::uint32_t> local_item() const;
  const SceneGraph& graph() const {return graph_;}
  const StageSetup& setup() const {return setup_;}
  bool busy() const {return playing_.has_value() || !pending_.empty();}
  bool puzzle() const {return puzzle_;}
  bool failed() const {return failed_;}
  bool boundary() const {return boundary_;}
  bool finished() const {return finished_;}
  std::uint32_t scene() const {return scene_;}
  unsigned area() const {return area_;}
  std::uint64_t remaining_ms() const {return deadline_>clock_?deadline_-clock_:0;}
};
}
