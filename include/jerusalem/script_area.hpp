#pragma once
#include "jerusalem/stage.hpp"
#include "jerusalem/x86.hpp"
#include <map>

namespace jerusalem {
// One area's registrations, recorded by running the original area setup routine
// (SetSpaceSoft(N) ... ) in X86Machine with every registration API trapped.
struct AreaSound { std::uint32_t id,category; std::string stem; std::uint32_t master=100; };
struct AreaAnimationEvent { std::uint32_t object,presentation,scene,event; bool panorama; };
struct AreaModel {
  unsigned area=0;
  StageSetup stage;          // objects, puzzle backgrounds, accesses, layers, dialogues, puzzle ambience
  SceneGraph graph;          // rotation nodes, rotation exits (to rotations and puzzles), rotation ambience
  std::vector<Exit> puzzle_exits;  // exits registered on puzzles (source is a puzzle ID)
  std::vector<std::uint32_t> puzzles;
  std::map<std::uint32_t,AreaSound> sounds;
  struct Variable { unsigned size; std::uint32_t value; };
  std::map<std::uint32_t,Variable> variables;  // initial values (VarDef*)
  std::map<std::uint32_t,std::uint32_t> object_flags;
  std::vector<AreaAnimationEvent> animation_events;
  std::map<std::string,unsigned> calls;  // registration API -> call count
  // Runtime engine calls made by the setup routine itself (e.g. initial visibility),
  // in order, for a runtime to replay. Variable definitions are in `variables`.
  struct RuntimeCall { std::uint32_t target; std::vector<std::uint32_t> args; };
  std::vector<RuntimeCall> runtime_calls;
};
// Setup routine entry for area 1..10.
std::uint32_t area_setup_function(unsigned area);
// Runs area's setup routine on `machine` (application object `app`, also stored at the
// original's global 0x4d0a90). Leaves the machine's traps set to the recorders.
AreaModel record_area_setup(X86Machine& machine,std::uint32_t app,unsigned area);
// Fills ambient stems/volumes from the area's own sounds, other areas, or the game-wide setup.
void resolve_area_sounds(Bytes executable,AreaModel& model,const std::map<unsigned,AreaModel>& known);
// Convenience: a fresh machine, one area.
AreaModel extract_area_model(Bytes executable,unsigned area);
}
