#include "jerusalem/stage.hpp"
#include "jerusalem/dialogue.hpp"
#include "jerusalem/checkpoint.hpp"
#include <algorithm>

namespace jerusalem {
StoryAssetTags a01_story_assets(const SceneGraph& graph,const StageSetup& setup,const std::filesystem::path& root){
  const auto assets=resolve_scene_assets(root,graph);
  auto tag=checkpoint_assets(graph,assets);std::uint64_t total=0;
  const auto data=[&](const std::filesystem::path& path){
    auto bytes=read_file(path);total+=bytes.size();if(total>512ULL*1024*1024)throw ParseError("story asset fingerprint limit");
    for(unsigned i=0;i<8;++i)tag=(tag^std::uint8_t(bytes.size()>>(8*i)))*1099511628211ULL;
    for(auto byte:bytes)tag=(tag^byte)*1099511628211ULL;
    return bytes;
  };
  (void)data(game_file(root,{"A01.AT3"}));(void)data(game_file(root,{"SY.AT3"}));
  for(const auto& name:{"A01S01_AN001","A01S01_AN002","A01S01_AN003","A01S01_AN004","A01S01_AN006","A01S01_AN007A","A01S01_AN007B","A01S01_AN007C","A01S01_AN009"})
    (void)data(game_file(root,{"A01","PLA","ENG",std::string(name)+".CI2"}));
  for(const auto id:{10999U,10000U,10001U,10002U,11000U,11002U,11004U,11005U,11008U,11011U,11012U,11013U,11014U,11015U,11016U,11017U}){
    const auto d=std::find_if(setup.dialogues.begin(),setup.dialogues.end(),[&](const DialogueResource& r){return r.id==id;});
    if(d==setup.dialogues.end())throw ParseError("refresh A01 connections from the original executable");
    const auto voice=resolve_dialogue(root,*d);(void)data(voice.audio);(void)data(voice.subtitles);
  }
  const auto legacy=tag;
  for(const auto& name:{"A01S01_AN006B","A01S01_AN010","A01S01_AN011","A01S01_AN011B","A01S01_AN013","A01S01_AN014"})
    (void)data(game_file(root,{"A01","PLA","ENG",std::string(name)+".CI2"}));
  for(const auto& d:setup.dialogues)if(d.id>=11018 && d.id<=11042){const auto voice=resolve_dialogue(root,d);(void)data(voice.audio);(void)data(voice.subtitles);}
  for(const auto& d:setup.dialogues){const auto voice=resolve_dialogue(root,d);if(voice.animation)(void)data(*voice.animation);}
  if(setup.objects.empty())throw ParseError("refresh A01 connections from the original executable for the full area");
  const auto text=data(game_file(root,{"KEYWORDS.INI"}));const auto keywords=parse_keywords(std::string(text.begin(),text.end()));
  for(const auto id:{11018U,11022U}){
    unsigned count=0;for(const auto& group:keywords)for(const auto& entry:group.topics)if(entry.id==id)++count;
    if(count!=1)throw ParseError("missing or ambiguous A01 topic in KEYWORDS.INI");
  }
  std::map<std::string,std::filesystem::path> sounds;
  for(const auto& bindings:{graph.ambient,setup.ambient})for(const auto& sound:bindings)sounds.emplace(sound.stem,resolve_ambient_asset(assets.begin()->second,sound.stem));
  for(const auto& sound:sounds)(void)data(sound.second);
  return {tag,legacy};
}
void validate_a02_assets(const StageSetup& setup,const std::filesystem::path& root){
  if(!stage_has_a02(setup))throw ParseError("stage model has no A02 records");
  (void)game_file(root,{"A02.AT3"});
  for(const auto& name:{"A02S02_AN016","A02S02_AN017","A02S02_AN018","A02S02_AN018B"})(void)game_file(root,{"A02","PLA","ENG",std::string(name)+".CI2"});
  std::optional<std::filesystem::path> sample;
  for(const auto& d:setup.dialogues)if(d.area=="A02"){const auto voice=resolve_dialogue(root,d);if(!sample)sample=voice.audio;}
  if(!sample)throw ParseError("stage model has no A02 speech");
  // resolve_ambient_asset searches the area folder above SOUND/ENG/<speech>.
  const auto area_marker=sample->parent_path().parent_path().parent_path()/"NODE"/"A02.AQC";
  for(const auto& a:setup.ambient)if(a.node>=20000 && a.node<30000)(void)resolve_ambient_asset(area_marker,a.stem);
  const auto text=read_file(game_file(root,{"KEYWORDS.INI"}));const auto keywords=parse_keywords(std::string(text.begin(),text.end()));
  for(const auto id:{22000U,100001U,22002U}){
    unsigned count=0;for(const auto& group:keywords)for(const auto& entry:group.topics)if(entry.id==id)++count;
    if(count!=1)throw ParseError("missing or ambiguous A02 topic in KEYWORDS.INI");
  }
}
}
