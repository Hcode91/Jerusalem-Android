#include "jerusalem/checkpoint.hpp"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
using namespace jerusalem;
namespace {
unsigned checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("checkpoint check "+std::to_string(checks));}
template<class F> void reject(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);}
SceneGraph graph(){
 SceneGraph g;g.nodes={{1,"SYNTH_A",2},{2,"SYNTH_B",2}};
 g.exits={{1,0,2,false,true,0,-100,100,100,53,0,{0,0,85}}};
 g.items={{10,1,0,-100,100,100,60,0,50,1,"Synthetic harness",{{1,0},{2,0}}},
 {11,1,100,-100,200,100,60,0,50,1,"Synthetic matting",{{1,1},{2,0}}},
 {12,1,200,-100,300,100,60,0,50,1,"Synthetic stone",{{2,1}}}};return g;
}
void put(const std::filesystem::path& path,const std::string& data){std::ofstream f(path,std::ios::binary);f<<data;if(!f)throw std::runtime_error("test write failed");}
}
int main(int argc,char** argv){try{
 const auto g=graph();const std::uint64_t asset_tag=0x123456789abcdef0ULL;
 Checkpoint cp{2,{90.5,-12.25,64},{11,10,12}};
 const auto bytes=encode_checkpoint(g,asset_tag,cp);check(bytes.size()==80);
 check(Bytes(bytes).is(0,std::string("JRNSAVE\0",8)));check(Bytes(bytes).u32(8)==1 && Bytes(bytes).u32(12)==1);
 check(Bytes(bytes).u32(24)==0x9abcdef0 && Bytes(bytes).u32(28)==0x12345678);
 check(Bytes(bytes).u32(32)==2 && Bytes(bytes).u32(36)==3 && Bytes(bytes).u32(64)==11);
 auto loaded=decode_checkpoint(Bytes(bytes),g,asset_tag);check(loaded.node==cp.node && loaded.inventory==cp.inventory);
 check(loaded.camera.yaw==90.5 && loaded.camera.pitch== -12.25 && loaded.camera.fov==64);
 std::vector<std::uint32_t> order{10,11,12};
 do{
  for(std::size_t count=0;count<=order.size();++count){
   InteractionState before(g);Checkpoint saved{1,{359.875,0.125,87},{order.begin(),order.begin()+count}};
   for(const auto id:saved.inventory)before.pickup(id);
   auto after=restore_interactions(g,decode_checkpoint(Bytes(encode_checkpoint(g,asset_tag,saved)),g,asset_tag));
   check(after.inventory()==before.inventory());check(after.variable(50)==before.variable(50));check(after.milestones()==before.milestones());
   for(const auto& node:g.nodes)check(after.visibility(node.id)==before.visibility(node.id));
   for(const auto& item:g.items)check(after.collected(item.id)==before.collected(item.id));
  }
 }while(std::next_permutation(order.begin(),order.end()));
 for(std::size_t i=0;i<bytes.size();++i)reject([&]{decode_checkpoint(Bytes(bytes).slice(0,i),g,asset_tag);});
 for(std::size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;reject([&]{decode_checkpoint(Bytes(bad),g,asset_tag);});}
 auto trailing=bytes;trailing.push_back(0);reject([&]{decode_checkpoint(Bytes(trailing),g,asset_tag);});
 reject([&]{decode_checkpoint(Bytes(bytes),g,asset_tag+1);});
 auto changed=g;changed.exits[0].enabled=false;reject([&]{decode_checkpoint(Bytes(bytes),changed,asset_tag);});
 auto renamed=g;renamed.items[0].name="Translated label";check(checkpoint_rules(renamed)==checkpoint_rules(g));check(decode_checkpoint(Bytes(bytes),renamed,asset_tag).inventory==cp.inventory);
 for(unsigned test=0;test<10;++test){auto bad=cp;switch(test){
 case 0:bad.node=99;break;case 1:bad.camera.yaw=360;break;case 2:bad.camera.yaw= -1;break;
 case 3:bad.camera.pitch=91;break;case 4:bad.camera.fov=29;break;case 5:bad.camera.fov=88;break;
 case 6:bad.camera.yaw=std::numeric_limits<double>::quiet_NaN();break;
 case 7:bad.camera.fov=std::numeric_limits<double>::infinity();break;
 case 8:bad.inventory={10,10};break;case 9:bad.inventory={999};break;
 }reject([&]{encode_checkpoint(g,asset_tag,bad);});reject([&]{restore_interactions(g,bad);});}
 auto no_items=g;no_items.items.clear();reject([&]{encode_checkpoint(no_items,asset_tag,{1,{},{}});});
 const auto root=std::filesystem::temp_directory_path()/("jerusalem-checkpoint-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 if(!std::filesystem::create_directory(root))throw std::runtime_error("cannot create checkpoint test directory");
 struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}}cleanup{root};
 const auto path=root/"native-saves/a01-preview.jrs";
 reject([&]{read_checkpoint(path,g,asset_tag);});
 write_checkpoint(path,g,asset_tag,cp);check(read_file(path)==bytes);check(read_checkpoint(path,g,asset_tag).inventory==cp.inventory);
 Checkpoint empty{1,{15,3,30},{}};write_checkpoint(path,g,asset_tag,empty);check(read_checkpoint(path,g,asset_tag).inventory.empty());
 const auto previous=read_file(path);auto invalid=cp;invalid.inventory={99};reject([&]{write_checkpoint(path,g,asset_tag,invalid);});check(read_file(path)==previous);
 const auto directory=root/"directory";std::filesystem::create_directory(directory);
 reject([&]{write_checkpoint(directory,g,asset_tag,cp);});check(std::filesystem::is_empty(directory));
 for(const auto& p:std::filesystem::directory_iterator(root))check(p.path().filename().string().find(".directory.")==std::string::npos);
 put(path,"invalid");reject([&]{read_checkpoint(path,g,asset_tag);});put(path,std::string(1024,'x'));reject([&]{read_checkpoint(path,g,asset_tag);});
 std::map<std::uint32_t,std::filesystem::path> assets{{1,root/"one"},{2,root/"two"}};
 put(assets[1],"original bytes A");put(assets[2],"original bytes B");const auto digest=checkpoint_assets(g,assets);
 write_checkpoint(path,g,digest,cp);check(read_checkpoint(path,g,checkpoint_assets(g,assets)).node==2);
 auto moved=assets;std::filesystem::rename(assets[1],root/"moved");moved[1]=root/"moved";check(checkpoint_assets(g,moved)==digest);
 put(moved[1],"changed bytes A");reject([&]{read_checkpoint(path,g,checkpoint_assets(g,moved));});
 auto missing=moved;missing.erase(2);reject([&]{checkpoint_assets(g,missing);});
 auto wrong=moved;wrong.erase(2);wrong.emplace(99,root/"two");reject([&]{checkpoint_assets(g,wrong);});
 if(argc==3){
  const auto executable=read_file(argv[1]);const auto original=extract_a01_navigation(Bytes(executable));
  const auto paths=resolve_scene_assets(argv[2],original);const auto tag=checkpoint_assets(original,paths);
  InteractionState before(original);for(auto it=original.items.rbegin();it!=original.items.rend();++it)before.pickup(it->id);
  const Checkpoint real{original.items.front().node,{25,0,85},before.inventory()};
  const auto native=root/"original-preview.jrs";write_checkpoint(native,original,tag,real);
  const auto saved=read_checkpoint(native,original,checkpoint_assets(original,paths));
  const auto restored=restore_interactions(original,saved);check(saved.node==real.node);check(restored.inventory()==before.inventory());
  for(const auto& item:original.items)check(restored.variable(item.counter)==before.variable(item.counter));
  for(const auto& node:original.nodes){
   check(restored.visibility(node.id)==before.visibility(node.id));
   const auto input=read_file(paths.at(node.id));const AqcAnimation animation(input);const auto image=decode_aqc(Bytes(input));
   check(compose_items(animation,image,restored.visibility(node.id)).rgba==compose_items(animation,image,before.visibility(node.id)).rgba);
  }
  std::cout<<"Original six-node checkpoint state and pixel restoration matched\n";
 }
 const auto huge=root/"oversized-node";
 {std::ofstream sparse(huge,std::ios::binary);sparse.seekp(max_resource_bytes);sparse.put(0);}
 auto oversized=moved;oversized[1]=huge;reject([&]{checkpoint_assets(g,oversized);});
 std::cout<<checks<<" checkpoint checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
