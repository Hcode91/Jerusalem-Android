#include "jerusalem/interactions.hpp"
#include "aqc_fixture.hpp"
#include <cmath>
#include <iostream>
using namespace jerusalem;
namespace {
unsigned checks=0;
void check(bool x){++checks;if(!x)throw std::runtime_error("interaction check "+std::to_string(checks));}
template<class F> void reject(F f){bool failed=false;try{f();}catch(const ParseError&){failed=true;}check(failed);}
SceneGraph graph(){
 SceneGraph g;g.nodes={{1,"SYNTH_A",2},{2,"SYNTH_B",2}};
 g.items={{10,1,0,-100,100,100,60,0,50,1,"Synthetic harness",{{1,0},{2,0}}},
          {11,1,100,-100,200,100,60,0,50,1,"Synthetic matting",{{1,1},{2,0}}}};
 return g;
}
std::vector<std::uint8_t> packet(){
 std::vector<std::uint8_t> b(79,0);const unsigned address=0x500000;
 const auto literal=[&](unsigned p,const std::string& bytes){std::copy(bytes.begin(),bytes.end(),b.begin()+p);};
 const auto word=[&](unsigned p,unsigned value){for(unsigned i=0;i<4;++i)b[p+i]=std::uint8_t(value>>(8*i));};
 const auto call=[&](unsigned p,unsigned target){b[p]=0xe8;word(p+1,target-(address+p+5));};
 b[0]=0x68;word(1,50);literal(5,std::string("\x8b\xce",2));call(7,0x42c1d0);
 literal(12,std::string("\xfe\xc0\x8b\xce\x50\x68",6));word(18,50);call(22,0x42c1a0);
 b[27]=0x6a;b[28]=1;call(29,0x432440);literal(34,std::string("\x83\xc4\x04\x8b\xce\x6a\x00\x68",8));word(42,10);call(46,0x42a9a0);
 b[51]=0x68;word(52,10);literal(56,std::string("\x8b\xce",2));call(58,0x429d70);
 b[63]=0x68;word(64,10);literal(68,std::string("\x8b\xce",2));call(70,0x42c3a0);literal(75,std::string("\x5e\xc2\x18\x00",4));return b;
}
}
int main(int argc,char** argv){try{
 auto g=graph();validate_navigation(g);const auto text=write_navigation(g);check(text.find("NAV_V3")!=std::string::npos);check(write_navigation(parse_navigation(text))==text);
 InteractionState state(g);check(state.inventory().empty() && state.variable(50)==0);check(state.visibility(1)==std::vector<bool>({true,true}));
 check(state.hit(1,0,-100)==10);check(!state.hit(1,-1,0));check(state.hit(1,100,0)==11);check(!state.hit(1,200,0));check(!state.hit(1,0,100));check(!state.hit(2,0,0));
 check(state.pickup(10));check(!state.pickup(10));check(state.inventory()==std::vector<std::uint32_t>{10});check(state.variable(50)==1);check(!state.hit(1,0,0));
 check(state.visibility(1)==std::vector<bool>({false,true}));check(state.visibility(2)==std::vector<bool>({false,false}));check(!state.collected(11));
 check(state.pickup(11));check(state.variable(50)==2);check(state.milestones()==std::set<std::uint32_t>{1});check(state.inventory()==std::vector<std::uint32_t>({10,11}));
 InteractionState reversed(g);check(reversed.pickup(11) && reversed.pickup(10));check(reversed.variable(50)==2);check(reversed.inventory()==std::vector<std::uint32_t>({11,10}));
 InteractionState fresh(g);check(fresh.variable(50)==0 && fresh.inventory().empty());reject([&]{state.pickup(999);});reject([&]{state.hit(999,0,0);});reject([&]{state.variable(99);});
 for(unsigned n=0;n<14;++n){auto bad=g;switch(n){
 case 0:bad.items[0].id=0;break;case 1:bad.items.push_back(bad.items[0]);break;case 2:bad.items[0].node=9;break;
 case 3:bad.items[0].right=0;break;case 4:bad.items[0].top= -901;break;case 5:bad.items[0].counter=0;break;
 case 6:bad.items[0].name="";break;case 7:bad.items[0].name="line\nbreak";break;case 8:bad.items[0].channels.clear();break;
 case 9:bad.items[0].channels[0].channel=2;break;case 10:bad.items[0].channels[0].node=9;break;
 case 11:bad.items[0].channels.push_back(bad.items[0].channels[0]);break;case 12:bad.items[0].milestone=128;break;case 13:bad.items[0].event=1;break;
 }reject([&]{validate_navigation(bad);});}
 reject([&]{parse_navigation(text+"item-channel 99 1 0\n");});reject([&]{parse_navigation(text+"item-channel 10 1 0 junk\n");});
 auto quoted=g;quoted.items[0].name="Synthetic \"quoted\" item";check(write_navigation(parse_navigation(write_navigation(quoted)))==write_navigation(quoted));
 auto b=packet();const auto effect=parse_pickup_effect(Bytes(b),0x500000);check(effect.object==10 && effect.counter==50 && effect.milestone==1);
 for(std::size_t i=0;i<b.size();++i)reject([&]{parse_pickup_effect(Bytes(b).slice(0,i),0x500000);});
 for(const auto p:{0,5,6,7,12,13,14,15,16,17,22,27,29,34,35,36,37,38,39,40,41,46,51,56,57,58,63,68,69,70,75,76,77,78}){auto bad=b;bad[p]^=1;reject([&]{parse_pickup_effect(Bytes(bad),0x500000);});}
 for(const auto p:{1,18,42,52,64}){auto bad=b;bad[p]^=1;reject([&]{parse_pickup_effect(Bytes(bad),0x500000);});}
 reject([&]{parse_pickup_effect(Bytes(b),0xfffffff0U);});
 auto combos=g;combos.combinations={{10,11,20,"Synthetic result"}};
 const auto v4=write_navigation(combos);check(v4.find("NAV_V4")!=std::string::npos);check(write_navigation(parse_navigation(v4))==v4);
 for(bool reverse:{false,true}) {
  InteractionState c(combos);check(!c.combine(10,11));c.pickup(10);check(!c.combine(10,11));c.pickup(11);
  const auto visible=c.visibility(2);const auto counter=c.variable(50);const auto markers=c.milestones();
  check(c.combine(reverse?11:10,reverse?10:11)==20);check(c.inventory()==std::vector<std::uint32_t>{20});
  check(c.collected(10) && c.collected(11) && !c.pickup(10) && !c.pickup(11));
  check(c.visibility(2)==visible && c.variable(50)==counter && c.milestones()==markers);
  check(c.history()==std::vector<std::uint32_t>({10,11,0x80000014U}));check(!c.combine(10,11));check(!c.combine(20,20));check(!c.combine(20,999));
 }
 check(inventory_name(combos,20)=="Synthetic result");check(inventory_name(combos,10)==g.items[0].name);reject([&]{inventory_name(combos,999);});
 for(unsigned n=0;n<11;++n){auto bad=combos;auto& c=bad.combinations[0];switch(n){
 case 0:c.first=999;break;case 1:c.second=c.first;break;case 2:c.result=0;break;case 3:c.result=c.first;break;
 case 4:c.result=0x80000000U;break;case 5:c.name="";break;case 6:c.name="bad\nname";break;case 7:c.name=std::string(96,'a');break;
 case 8:bad.combinations.push_back(c);break;case 9:bad.combinations.push_back({11,10,21,"Other"});break;
 case 10:bad.combinations.resize(17,c);break;
 }reject([&]{validate_navigation(bad);});}
 reject([&]{parse_navigation(v4+"combine 10 11 20 \"Extra\" junk\n");});
 auto data=fixture::base();for(unsigned left:{0,4}){fixture::integer(data,1);fixture::real(data,0);fixture::integer(data,0);fixture::append(data,fixture::frame(left,left?2:0,1));}
 const AqcAnimation animation(data);const auto base=decode_aqc(Bytes(data));InteractionState pixels(g);
 const auto both=compose_items(animation,base,pixels.visibility(1));check(both.rgba!=base.rgba && both.rgba[1]==252);
 pixels.pickup(10);const auto after=compose_items(animation,base,pixels.visibility(1));check(after.rgba[1]==base.rgba[1] && after.rgba[(2*8+4)*4+1]==252);
 pixels.pickup(11);check(compose_items(animation,base,pixels.visibility(1)).rgba==base.rgba);
 reject([&]{compose_items(animation,base,{true});});reject([&]{compose_items(AqcAnimation(fixture::animated()),base,{false,true});});
 // Optional private corpus validates all six nodes before/after each pickup.
 if(argc==3){const auto bytes=read_file(argv[1]);auto original=extract_a01_navigation(Bytes(bytes));check(original.items.size()==3);
  InteractionState real(original);
  for(unsigned step=0;step<=original.items.size();++step){
   for(const auto& node:original.nodes){auto input=read_file(resolve_scene_assets(argv[2],original).at(node.id));const auto image=decode_aqc(Bytes(input));const auto composed=compose_items(AqcAnimation(input),image,real.visibility(node.id));check(composed.rgba.size()==image.rgba.size());}
   if(step<original.items.size())check(real.pickup(original.items[step].id));
  }
  check(real.inventory().size()==3 && real.variable(original.items[0].counter)==3);
 }
 std::cout<<checks<<" interaction checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
