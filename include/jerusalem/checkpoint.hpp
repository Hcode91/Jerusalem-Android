#pragma once
#include "jerusalem/interactions.hpp"
namespace jerusalem {
// Independent native preview format. Never an original Windows .ARS save.
struct Checkpoint {
  std::uint32_t node;
  Camera camera;
  // Ordered action log: pickup ID, bit31 | combination result ID (revision 2),
  // or bit30 | consuming item-use rule ID (revision 3).
  // Retains the revision-1 field name for source compatibility.
  std::vector<std::uint32_t> inventory;
};
std::uint64_t checkpoint_rules(const SceneGraph& graph);
std::uint64_t checkpoint_assets(const SceneGraph& graph,const std::map<std::uint32_t,std::filesystem::path>& assets);
InteractionState restore_interactions(const SceneGraph& graph,const Checkpoint& checkpoint);
std::vector<std::uint8_t> encode_checkpoint(const SceneGraph& graph,std::uint64_t assets,const Checkpoint& checkpoint);
Checkpoint decode_checkpoint(Bytes bytes,const SceneGraph& graph,std::uint64_t assets);
void write_checkpoint(const std::filesystem::path& path,const SceneGraph& graph,std::uint64_t assets,const Checkpoint& checkpoint);
Checkpoint read_checkpoint(const std::filesystem::path& path,const SceneGraph& graph,std::uint64_t assets);
// Shared atomic writer for independent native save formats.
void write_save_file(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes);
}
