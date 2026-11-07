#include "jerusalem/stage.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <iostream>
using namespace jerusalem;
namespace {
unsigned checks=0;
std::function<void(const A01Story&)> inspect_picture;
std::function<void(const A01Story&)> inspect_save;
void verify(bool ok,int line){++checks;if(!ok)throw std::runtime_error("stage check "+std::to_string(checks)+" at line "+std::to_string(line));}
#define check(value) verify(bool(value),__LINE__)
template<class F>void reject(F f){bool caught=false;try{f();}catch(const ParseError&){caught=true;}check(caught);}
SceneGraph graph(){
 SceneGraph g;
 for(unsigned i=1;i<=6;++i)g.nodes.push_back({1010100+i,"SYNTH_"+std::to_string(i),10});
 for(unsigned i=1;i<6;++i){g.exits.push_back({1010100+i,0,1010101+i,false,true,0,0,10,10,53,0,{}});g.exits.push_back({1010101+i,1,1010100+i,false,true,0,0,10,10,53,0,{}});}
 g.items={{10002,1010103,0,0,10,10,60,0,10001,1,"Synthetic harness",{{1010103,3}}},
 {10003,1010103,10,0,20,10,60,0,10001,1,"Synthetic matting",{{1010103,0}}},
 {10004,1010103,20,0,30,10,60,0,10001,1,"Synthetic stone",{{1010103,2}}},
 {10001,1010105,0,0,10,10,60,0,10002,0,"Synthetic palm",{{1010105,0}},true,1}};
 g.combinations={{10002,10004,10017,"Synthetic sling"}};
 g.targets={{10006,1010105,0,0,10,10,62,0,false,"Synthetic character"},{10007,1010105,0,0,10,10,62,0,true,"Synthetic offer"}};
 g.presentations={{10006,0,{{1010105,2}}},{10006,1,{{1010105,5}}}};
 g.uses={{1,10007,10003,10003,1,10006,10002,10006,1,0,0,true}};
 validate_navigation(g);return g;
}
StageSetup setup(){
 StageSetup s;
 for(auto id:{10000U,10001U,10016U,10017U,10018U,10019U,10020U,10021U,10023U,10026U,10027U,10028U,10038U,10039U,10043U,10044U,10045U})s.backgrounds.push_back({id,"SYNTH.BMP",0,16});
 for(auto id:{10002U,10003U,10004U,10001U,10006U,10008U,10009U,10007U,10010U,10011U})s.access.push_back({id,id>=10002 && id<=10004?1010103U:1010105U,0,true,id!=10006 && id!=10008 && id!=10009 && id!=10011,0,0,10,10,62,0});
 s.access.push_back({10008,10000,1,false,true,0,16,639,181,62,1});
 s.access.push_back({10005,10000,0,false,true,271,133,312,172,61,0});
 for(auto id:{10012U,10013U})s.access.push_back({id,10000,0,false,true,100,180,120,240,62,0});
 s.layers={{10005,0,10000,false,true,"SYNTH",0,3,16,500,271,145,12.5}};
 for(auto id:{10999U,10000U,10001U,10002U,11000U,11002U,11004U,11005U,11008U,11011U,11012U,11013U,11014U,11015U,11016U,11017U})s.dialogues.push_back({id,"A01","SYNTH_"+std::to_string(id)});
 return parse_stage(write_stage(s));
}
std::vector<StoryCue> drain(A01Story& story){std::vector<StoryCue> cues;unsigned budget=100;while(auto cue=story.next()){check(budget-->0);cues.push_back(*cue);if(inspect_save)inspect_save(story);if(inspect_picture && (cue->kind==StoryCue::Node || cue->kind==StoryCue::Puzzle))inspect_picture(story);if(cue->kind==StoryCue::Movie || cue->kind==StoryCue::Speech){check(!story.complete(cue->serial+1));check(story.complete(cue->serial));check(!story.complete(cue->serial));}}return cues;}
void route(const SceneGraph& g,const StageSetup& s,bool sling,std::uint32_t attack=10005,bool no_ammo=false){
 InteractionState inventory(g);A01Story story(s,g,inventory);
 inspect_save=[&](const A01Story& source){
   StoryResume resume{{36.5,-12,65},source.playing()?123U:0U,3200,{}};
   const auto bytes=source.save(resume,77);InteractionState restored_items(g);A01Story restored(s,g,restored_items);
   const auto loaded=restored.load(Bytes(bytes),77);check(loaded.camera.yaw==36.5 && loaded.camera.pitch== -12 && loaded.media_ms==resume.media_ms);
   if(source.playing()){const auto cue=restored.next();check(cue && cue->serial==source.playing()->serial && cue->kind==source.playing()->kind);}
   check(restored.save(loaded,77)==bytes);check(restored_items.inventory()==inventory.inventory());check(restored.remaining_ms()==source.remaining_ms());
 };
 story.start();reject([&]{story.start();});
 auto opening=drain(story);check(opening.size()==5);check(opening[0].asset=="A01S01_AN001" && opening[2].id==10999 && opening[3].asset=="A01S01_AN002");check(story.scene()==1010101 && !story.puzzle());check(!story.activate(10005,0,10017));
 check(!story.enter_node(1010106));check(!story.pickup(10002));
 check(story.enter_node(1010102));drain(story);check(story.enter_node(1010103));drain(story);
 check(story.pickup(10002));check(story.pickup(10003));if(!no_ammo){check(story.pickup(10004));check(!story.pickup(10004));}
 if(sling)check(inventory.combine(10002,10004)==10017);
 check(story.enter_node(1010104));drain(story);check(story.enter_node(1010105));drain(story);check(story.pickup(10001));
 check(story.activate(10007,0,10003));check(!inventory.owns(10003));check(story.local_item()==10010);
 check(story.activate(10006,0,10010));check(!story.local_item());check(story.activate(10008,0));check(story.activate(10009,0));check(story.activate(10008,0));
 check(!story.activate(10011,0,10001));
 check(story.enter_node(1010106));auto encounter=drain(story);check(encounter.size()==9 && encounter[1].asset=="A01S01_AN003");check(story.remaining_ms()==60000);
 check(story.enter_puzzle(10039));auto warning=drain(story);check(warning.size()==4 && warning[1].id==11004 && warning[2].id==11005);check(story.scene()==1010105);
 {
   const auto bytes=story.save({{0,0,85},0,15000,10001},88);InteractionState restored_items(g);A01Story restored(s,g,restored_items);const auto resume=restored.load(Bytes(bytes),88);check(resume.selected==10001);check(restored.activate(10011,0,resume.selected));const auto movie=restored.next();check(movie && movie->asset=="A01S01_AN004");
 }
 check(story.activate(10011,0,10001));
 if(no_ammo){const auto movie=story.next();check(movie && movie->kind==StoryCue::Movie);story.tick(90000);check(!story.failed());check(story.complete(movie->serial));drain(story);check(story.remaining_ms()==8000);story.tick(7999);check(!story.failed());story.tick(1);check(story.failed());auto warning=drain(story);check(warning.size()==1 && warning[0].id==10019);story.tick(2000);auto over=drain(story);check(over.size()==1 && over[0].id==10043);inspect_save={};return;}
 auto escape=drain(story);check(escape.size()==2 && escape[0].asset=="A01S01_AN004" && story.puzzle() && story.scene()==10000);check(story.remaining_ms()==0);
 check(story.hit(280,150)->object==10008);check(story.activate(10008,1));check(story.hit(280,150)->object==10005);
 check(story.activate(attack,0,sling?10017:10004));auto finale=drain(story);
 if(attack!=10005){check(story.failed());check(finale.size()==2 && finale[0].asset==(attack==10012?"A01S01_AN007A":"A01S01_AN007B") && finale[1].id==10044);inspect_save={};return;}
 check(finale.front().asset=="A01S01_AN007C");check(std::any_of(finale.begin(),finale.end(),[](const StoryCue& c){return c.asset=="A01S01_AN009";}));
 check(story.boundary() && story.scene()==10023 && !story.failed());check(inventory.inventory().empty());check(!story.activate(10005,0,10017));inspect_save={};
}
}
int main(int argc,char** argv){try{
 const auto g=graph();const auto s=setup();const auto text=write_stage(s);check(write_stage(parse_stage(text))==text);
 for(const auto bad:{std::string("bad"),text+"unknown 1\n",text+"access 1 10000 0 2 1 0 0 10 10 1 1\n",text+"background 1 ../BAD 0 0\n",text+"background 10000 X 0 0\n"})reject([&]{parse_stage(bad);});
 check(stage_point(640,360,1280,720)->x==320);check(stage_point(640,360,1280,720)->y==240);check(!stage_point(0,0,1280,720));check(!stage_point(0,0,0,0));check(!stage_point(640,0,640,480));
 Bitmap a{2,1,{10,20,30,255,40,50,60,255}},b{2,1,{200,100,0,0,80,90,100,255}};stage_blit(a,b,0,0);check(a.rgba==std::vector<std::uint8_t>({10,20,30,255,80,90,100,255}));stage_blit(a,b,-1,0);check(a.rgba[0]==80);
 reject([&]{Bitmap bad{0x80000000U,0x80000000U,{}};stage_blit(bad,b,0,0);});
 for(unsigned alpha=0;alpha<256;++alpha){Bitmap destination{1,1,{199,128,255,255}},source{1,1,{211,99,10,static_cast<std::uint8_t>(alpha)}};stage_blit_tga(destination,source,0,0);check(destination.rgba[0]==(211*alpha/256+199*(255-alpha)/256));check(destination.rgba[1]==(99*alpha/256+128*(255-alpha)/256));check(destination.rgba[2]==(10*alpha/256+255*(255-alpha)/256));}
 auto l=s.layers.front();for(unsigned t=0;t<12;++t){const unsigned expected[]={0,1,2,2,1,0,0,1,2,2,1,0};check(stage_frame(t*80,l)==expected[t]);}
 l.mode=3;reject([&]{stage_frame(80,l);});
 const auto root=std::filesystem::temp_directory_path()/("jerusalem-stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directories(root/"data/a01");{std::ofstream f(root/"data/a01/SOUND.WAV");f<<"synthetic";}
 check(game_file(root,{"A01","sound.wav"})==root/"data/a01/SOUND.WAV");
 reject([&]{game_file(root,{"..","SOUND.WAV"});});reject([&]{game_file(root,{"A01","MISSING"});});
 {std::ofstream f(root/"data/a01/sound.wav");f<<"duplicate";}reject([&]{game_file(root,{"A01","sound.wav"});});std::filesystem::remove(root/"data/a01/sound.wav");
 std::filesystem::create_symlink(root/"data/a01/SOUND.WAV",root/"data/a01/LINK.WAV");reject([&]{game_file(root,{"A01","LINK.WAV"});});std::filesystem::remove_all(root);
 route(g,s,true);route(g,s,false);route(g,s,true,10012);route(g,s,true,10013);route(g,s,false,10005,true);
 InteractionState saved_items(g);A01Story saved_story(s,g,saved_items);saved_story.start();auto current=saved_story.next();check(current && current->kind==StoryCue::Movie);
 const StoryResume movie_resume{{0,0,85},1000,5000,{}};const auto state=saved_story.save(movie_resume,19);
 for(std::size_t n=0;n<state.size();++n){auto bad=state;bad[n]^=1;reject([&]{saved_story.load(Bytes(bad),19);});check(saved_story.save(movie_resume,19)==state);}
 for(std::size_t n=0;n<state.size();++n)reject([&]{saved_story.load(Bytes(state).slice(0,n),19);});
 reject([&]{saved_story.load(Bytes(state),20);});
 auto changed=s;changed.backgrounds.front().image="OTHER.BMP";InteractionState other_items(g);A01Story other(changed,g,other_items);reject([&]{other.load(Bytes(state),19);});
 InteractionState items(g);A01Story timeout(s,g,items);timeout.start();drain(timeout);for(unsigned id=1010102;id<=1010106;++id){check(timeout.enter_node(id));drain(timeout);}timeout.tick(59999);check(!timeout.failed());
 const auto deadline_save=timeout.save({{0,0,85},0,59999,{}},31);InteractionState deadline_items(g);A01Story deadline_story(s,g,deadline_items);deadline_story.load(Bytes(deadline_save),31);check(deadline_story.remaining_ms()==1);deadline_story.tick(1);check(deadline_story.failed());
 timeout.tick(1);check(timeout.failed());auto failure=drain(timeout);check(failure.size()==4 && failure[0].id==10038 && failure[1].id==11002 && failure[2].asset=="A01S01_AN006" && failure[3].id==10043);
 if(argc>1){const auto exe=read_file(argv[1]);const auto original=extract_a01_stage(Bytes(exe));const auto original_graph=extract_a01_navigation(Bytes(exe));check(original.backgrounds.size()==25);check(original.dialogues.size()>=35);if(argc>2){
    const std::filesystem::path root=argv[2];StageArt art(read_file(game_file(root,{"A01.AT3"}))),system(read_file(game_file(root,{"SY.AT3"})));
    const auto image=[&](const std::string& name){try{return art.image(name);}catch(const ParseError& e){if(std::string(e.what()).find("missing AT3 image:")!=0)throw;return system.image(name);}};
    inspect_picture=[&](const A01Story& story){
      if(story.puzzle()){
        Bitmap canvas{640,480,std::vector<std::uint8_t>(640*480*4,0)};
        for(const auto& b:story.setup().backgrounds)if(b.id==story.scene())stage_blit(canvas,image(b.image),b.x,b.y);
        for(const auto& l:story.layers())if(!l.panorama)for(unsigned i=0;i<l.frames;++i){std::ostringstream name;name<<l.image;if(l.animated)name<<'.'<<std::setw(4)<<std::setfill('0')<<i+1<<(l.channel?".TGA":".BMP");if(l.channel)stage_blit_tga(canvas,image(name.str()),l.x,l.y);else stage_blit(canvas,image(name.str()),l.x,l.y);check(canvas.rgba.size()==640*480*4);}
      }else{
        const auto data=read_file(resolve_scene_assets(root,original_graph).at(story.scene()));AqcAnimation a(data);const auto visibility=story.visibility(story.scene());check(visibility.size()==a.channels().size());
        auto canvas=decode_aqc(Bytes(data));
        for(const auto& l:story.layers())if(l.panorama){check(l.channel<a.channels().size());for(unsigned i=0;i<l.frames;++i){const auto& r=a.channels()[l.channel].frames.at(i);apply_aqc_patch(canvas,a.frame(l.channel,i),r.left,r.top);}}
      }
    };
    route(original_graph,original,true);route(original_graph,original,false);inspect_picture={};
  }else{route(original_graph,original,true);route(original_graph,original,false);}
  std::cout<<"Original registration + first-escape routes passed\n";}
 std::cout<<checks<<" stage checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
