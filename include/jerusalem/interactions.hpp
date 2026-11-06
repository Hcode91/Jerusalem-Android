#pragma once
#include "jerusalem/navigation.hpp"
#include "jerusalem/aqc_animation.hpp"
#include <set>
namespace jerusalem {
struct PickupEffect { std::uint32_t object,counter,milestone; };
PickupEffect parse_pickup_effect(Bytes code,std::uint32_t address);
// Fresh, deliberately limited pickup state. No original saves or item-use rules.
class InteractionState {
  SceneGraph graph_;
  std::map<std::uint32_t,std::vector<bool>> visibility_;
  std::map<std::uint32_t,std::uint8_t> variables_;
  std::set<std::uint32_t> collected_,milestones_;
  std::vector<std::uint32_t> inventory_;
 public:
  explicit InteractionState(const SceneGraph& graph);
  std::optional<std::uint32_t> hit(std::uint32_t node,std::int32_t x,std::int32_t y) const;
  bool pickup(std::uint32_t item);
  bool collected(std::uint32_t item) const;
  const std::vector<bool>& visibility(std::uint32_t node) const;
  std::uint8_t variable(std::uint32_t key) const;
  const std::set<std::uint32_t>& milestones() const { return milestones_; }
  const std::vector<std::uint32_t>& inventory() const { return inventory_; }
};
const Collectible& collectible(const SceneGraph& graph,std::uint32_t id);
Bitmap compose_items(const AqcAnimation& animation,const Bitmap& base,const std::vector<bool>& visible);
}
