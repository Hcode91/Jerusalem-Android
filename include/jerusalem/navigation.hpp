#pragma once
#include "jerusalem/panorama.hpp"
#include <optional>
#include <map>

namespace jerusalem {
struct SetupCall { std::uint32_t address,target; std::vector<std::uint32_t> arguments; };
std::vector<SetupCall> parse_setup_calls(Bytes code,std::uint32_t address);
struct SceneNode { std::uint32_t id; std::string asset; std::uint32_t channels; };
struct Exit {
  std::uint32_t source,slot,target;
  bool puzzle,enabled;
  std::int32_t left,top,right,bottom;
  std::uint32_t cursor,event;
  Camera arrival;
};
struct SceneGraph { std::vector<SceneNode> nodes; std::vector<Exit> exits; };
SceneGraph extract_a01_navigation(Bytes executable);
std::string write_navigation(const SceneGraph& graph);
SceneGraph parse_navigation(const std::string& text);
void validate_navigation(const SceneGraph& graph);
const SceneNode& scene_node(const SceneGraph& graph,std::uint32_t id);
bool exit_contains(const Exit& exit,std::int32_t x,std::int32_t y);
std::optional<std::size_t> navigation_hit(const SceneGraph& graph,std::uint32_t source,std::int32_t x,std::int32_t y);
std::map<std::uint32_t,std::filesystem::path> resolve_scene_assets(const std::filesystem::path& root,const SceneGraph& graph);
struct HotspotPoint { std::int32_t x,y; };
HotspotPoint hotspot_point(const PanoramaGeometry& geometry,TexturePoint texture);
// Normalized screen position of an angular hotspot centre; null when behind/offscreen.
std::optional<TexturePoint> hotspot_screen(const PanoramaGeometry& geometry,const Camera& camera,
                                         double yaw_tenths,double pitch_tenths,unsigned width,unsigned height);
}
