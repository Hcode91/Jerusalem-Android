#pragma once
#include "jerusalem/navigation.hpp"
#include "jerusalem/aqc_animation.hpp"
#include <set>
namespace jerusalem {
struct PickupEffect { std::uint32_t object,counter,milestone; };
PickupEffect parse_pickup_effect(Bytes code,std::uint32_t address);
// Fresh preview state: pickups and verified, consuming two-item combinations.
class InteractionState {
  SceneGraph graph_;
  std::map<std::uint32_t,std::vector<bool>> visibility_;
  std::map<std::uint32_t,std::uint8_t> variables_;
  std::set<std::uint32_t> collected_,milestones_,enabled_targets_,used_;
  std::vector<std::uint32_t> inventory_,history_;
  void presentation(std::uint32_t object,std::uint32_t index,bool shown);
 public:
  explicit InteractionState(const SceneGraph& graph);
  std::optional<std::uint32_t> hit(std::uint32_t node,std::int32_t x,std::int32_t y) const;
  bool pickup(std::uint32_t item);
  std::optional<std::uint32_t> combine(std::uint32_t first,std::uint32_t second);
  const std::vector<std::uint32_t>& history() const { return history_; }
  bool owns(std::uint32_t item) const;
  bool target_enabled(std::uint32_t target) const;
  std::optional<std::uint32_t> target_hit(std::uint32_t node,std::int32_t x,std::int32_t y) const;
  std::optional<std::uint32_t> use_match(std::uint32_t target,std::uint32_t item) const;
  bool use(std::uint32_t rule);
  bool collected(std::uint32_t item) const;
  const std::vector<bool>& visibility(std::uint32_t node) const;
  std::uint8_t variable(std::uint32_t key) const;
  const std::set<std::uint32_t>& milestones() const { return milestones_; }
  const std::vector<std::uint32_t>& inventory() const { return inventory_; }
};
std::string inventory_name(const SceneGraph& graph,std::uint32_t id);
const Collectible& collectible(const SceneGraph& graph,std::uint32_t id);
Bitmap compose_items(const AqcAnimation& animation,const Bitmap& base,const std::vector<bool>& visible);
}
