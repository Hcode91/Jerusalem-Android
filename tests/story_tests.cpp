#include "jerusalem/checkpoint.hpp"
#include "jerusalem/dialogue.hpp"
#include "jerusalem/cinema.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace jerusalem;
namespace {
unsigned checks=0;
void check(bool v){++checks;if(!v)throw std::runtime_error("story/media check "+std::to_string(checks));}
template<class F>void reject(F f){bool caught=false;try{f();}catch(const ParseError&){caught=true;}check(caught);}
SceneGraph graph(){
 SceneGraph g;g.nodes={{1,"SYNTH_A",4},{2,"SYNTH_B",4}};
 g.items={{10,1,0,0,100,100,60,0,50,1,"Harness",{{1,0}}},{11,1,100,0,200,100,60,0,50,1,"Matting",{{1,1}}},{12,1,200,0,300,100,60,0,50,1,"Stone",{{1,2}}},{13,2,0,0,100,100,60,0,51,0,"Palm",{{2,0}},true,1}};
 g.combinations={{10,12,20,"Sling"}};
 g.presentations={{30,0,{{1,3},{2,1}}},{30,1,{{2,2}}}};
 g.pickup_switches={{13,52,1,30,1,0}};
 g.targets={{30,2,100,0,200,100,62,0,false,"Successor"},{31,2,100,0,200,100,62,0,true,"Offer"},{32,1,300,0,400,100,62,0,true,"Feedback"}};
 g.uses={{1,31,11,52,1,30,51,30,1,0,0,true},{2,32,20,0,0,0,0,0,0,0,60,false}};
 g.dialogues={{60,"A01","SYNTH_SPEECH"}};return g;
}
void word(std::vector<std::uint8_t>& b,std::size_t p,std::uint32_t v,unsigned bytes=4){if(b.size()<p+bytes)b.resize(p+bytes);for(unsigned i=0;i<bytes;++i)b[p+i]=std::uint8_t(v>>(8*i));}
struct BitWriter {std::vector<std::uint8_t> bytes;unsigned count=0;void put(unsigned v,unsigned n){for(unsigned i=0;i<n;++i){if(count%8==0)bytes.push_back(0);bytes[count/8]|=((v>>i)&1)<<(count%8);++count;}}};
std::vector<std::uint8_t> cinema(bool sound=false){
 std::vector<std::uint8_t> b(192+(sound?16:0)+8);std::copy_n("CNM UNR\0",8,b.begin());word(b,17,32);word(b,21,2);b[25]=32;b[27]=sound;word(b,28,1);
 if(sound){b[192]=1;b[193]=16;word(b,194,22050,2);b.push_back('Z');const auto p=b.size();word(b,p,4);b.insert(b.end(),{1,0,2,0});}
 BitWriter indices;indices.put(0,1);for(unsigned i=0;i<8;++i){indices.put(0,3);indices.put(1,1);}indices.put(1,1);
 const auto offset=indices.bytes.size();std::vector<std::uint8_t> payload=indices.bytes;
 for(unsigned i=0;i<4;++i)payload.insert(payload.end(),{5,7,9,0});payload.insert(payload.end(),{0,0});
 b.push_back('S');const auto p=b.size();b.resize(p+47);word(b,p,payload.size());word(b,p+4,offset);word(b,p+8,1,2);word(b,p+10,4,2);word(b,p+12,32);word(b,p+16,2);b.insert(b.end(),payload.begin(),payload.end());return b;
}
std::vector<std::uint8_t> advanced_cinema(bool overshoot=false){
 const unsigned w=overshoot?12:32,h=overshoot?3:2,n=3;
 std::vector<std::uint8_t> b(200);std::copy_n("CNM UNR\0",8,b.begin());word(b,17,w);word(b,21,h);b[25]=32;word(b,28,1);word(b,12,2500);
 BitWriter indices;indices.put(0,1);for(unsigned i=0;i<8;++i){indices.put(0,3);indices.put(1,2);}
 indices.put(0,1);
 if(overshoot){indices.put(0,3);indices.put(1,2);indices.put(4,3);indices.put(15,4);indices.put(1,1);for(unsigned i=0;i<6;++i)indices.put(1,1);}
 else {
  indices.put(0,3);indices.put(2,2); // Literal below up=1 inserts MRU[1][0]=2.
  indices.put(6,3);indices.put(0,4); // Read that MRU slot without updating it.
  indices.put(4,3);indices.put(0,4);indices.put(0,1); // up+1.
  indices.put(2,3); // Unique left candidate 2; no selector bits.
  indices.put(1,1);indices.put(1,1); // Two up copies.
  indices.put(0,3);indices.put(3,2);indices.put(0,3);indices.put(3,2);
 }
 auto payload=indices.bytes;const auto offset=payload.size();payload.insert(payload.end(),16,0);
 BitWriter book;for(unsigned entry=1;entry<=n;++entry)for(unsigned column=0;column<4;++column){book.put(7,3);for(unsigned lane=0;lane<4;++lane)book.put(column==0?entry*10:column==3?0:entry,8);}
 payload.insert(payload.end(),book.bytes.begin(),book.bytes.end());
 b.push_back('S');const auto p=b.size();b.resize(p+47);word(b,p,payload.size());word(b,p+4,offset);word(b,p+8,n,2);word(b,p+10,4,2);word(b,p+12,w);word(b,p+16,h);b.insert(b.end(),payload.begin(),payload.end());return b;
}

}
int main(int argc,char** argv){try{
 const auto g=graph();const auto text=write_navigation(g);check(text.find("NAV_V5")!=std::string::npos);check(write_navigation(parse_navigation(text))==text);
 const auto legacy=legacy_navigation(g,2);check(legacy.items.size()==3 && !has_story_rules(legacy));
 const Checkpoint old{1,{0,0,85},{11,10,12}};const auto older=encode_checkpoint(legacy,7,old);check(decode_checkpoint(Bytes(older),g,7).inventory==old.inventory);
 const auto legacy_one=legacy_navigation(g,1);check(decode_checkpoint(Bytes(encode_checkpoint(legacy_one,7,old)),g,7).inventory==old.inventory);
 const Checkpoint legacy_sling{1,{0,0,85},{10,12,0x80000014U}};
 const auto migrated_sling=restore_interactions(g,decode_checkpoint(Bytes(encode_checkpoint(legacy,7,legacy_sling)),g,7));check(migrated_sling.owns(20) && migrated_sling.history()==legacy_sling.inventory);
 for(bool palm_first:{false,true}){
  InteractionState s(g);s.pickup(11);if(palm_first)s.pickup(13);
  check(s.target_hit(2,150,50)==31);check(s.use_match(31,11)==1);check(s.use(1));check(!s.owns(11) && s.collected(11));check(s.variable(52)==1 && s.variable(50)==1);check(s.target_enabled(30));check(s.target_hit(2,150,50)==30);
  check(s.visibility(2)[palm_first?1:2]);check(!s.use(1));check(!s.pickup(11));check(!s.use_match(31,12));
  if(!palm_first){check(s.pickup(13));check(!s.visibility(2)[2] && s.visibility(2)[1]);}
  check(s.variable(51)==1 && s.variable(50)==1 && s.milestones()==std::set<std::uint32_t>{1});
  s.pickup(10);s.pickup(12);check(s.combine(12,10)==20);const auto history=s.history();check(s.use_match(32,20)==2);check(s.use(2));check(s.owns(20) && s.history()==history);
  const Checkpoint saved{2,{10,-5,80},s.history()};const auto encoded=encode_checkpoint(g,7,saved);check(Bytes(encoded).u32(12)==3);
  const auto restored=restore_interactions(g,decode_checkpoint(Bytes(encoded),g,7));check(restored.inventory()==s.inventory());check(restored.visibility(2)==s.visibility(2));check(restored.target_enabled(30));check(restored.variable(52)==1);
  for(std::size_t i=0;i<encoded.size();++i)reject([&]{decode_checkpoint(Bytes(encoded).slice(0,i),g,7);});
 }
 for(auto history:std::vector<std::vector<std::uint32_t>>{{0x40000001},{11,0x40000002},{11,0x40000001,0x40000001},{11,0x40000001,11},{0xc0000001},{13,13}})reject([&]{encode_checkpoint(g,7,{1,{0,0,85},history});});
 for(unsigned n=0;n<14;++n){auto bad=g;switch(n){case 0:bad.items.back().value=256;break;case 1:bad.items.back().milestone=1;break;case 2:bad.targets[1].id=30;break;case 3:bad.targets[0].right=0;break;case 4:bad.presentations[0].channels[0].channel=9;break;case 5:bad.pickup_switches[0].show=9;break;case 6:bad.uses[0].choice=0;break;case 7:bad.uses[0].consume=false;break;case 8:bad.uses[1].dialogue=99;break;case 9:bad.dialogues[0].stem="../BAD";break;case 10:bad.uses[0].enable=99;break;case 11:bad.uses.push_back(bad.uses[0]);break;case 12:bad.pickup_switches.push_back(bad.pickup_switches[0]);break;case 13:bad.presentations.push_back(bad.presentations[0]);break;}reject([&]{write_navigation(bad);});}
 const auto dia=parse_dia("0 First#Alternative\r\n1402 Second\r\n4045 END\r\n");check(dia.cues.size()==2 && dia.cues[0].secondary=="Alternative");check(subtitle_at(dia,0)=="First");check(subtitle_at(dia,1402)=="First");check(subtitle_at(dia,1403)=="Second");check(subtitle_at(dia,4045)=="Second");check(subtitle_at(dia,4046).empty());
 check(parse_dia(std::string("0 Caf\xe9\xa0!\n1 END\n")).cues[0].text=="Caf\xc3\xa9 !");
 for(const auto& invalid:{"", "0 Caption\n", "0 END\n", "5 A\n4 END\n", "0 A\n0 END\n", "0 A\n1 END\n2 B\n", "1,00:00.00 A\n2 END\n", "999999999999999999 A\n1 END\n"})reject([&]{parse_dia(invalid);});
 for(bool sound:{false,true}){
  const auto bytes=cinema(sound);CinemaDecoder decoder(bytes);check(decoder.info().frames==1);if(sound)check(decoder.info().sounds[0].channels==2);unsigned frames=0,audio=0;
  while(!decoder.done()){const auto event=decoder.next();if(event.kind==CinemaEvent::Audio){check(event.pcm==std::vector<std::uint8_t>({1,0,2,0}));++audio;}if(event.kind==CinemaEvent::Frame)++frames;}
  check(frames==1 && audio==unsigned(sound));const auto image=decoder.image();check(image.width==32 && image.height==2);for(std::size_t i=0;i<image.rgba.size();i+=4)check(image.rgba[i]==9 && image.rgba[i+1]==7 && image.rgba[i+2]==5 && image.rgba[i+3]==255);
  for(std::size_t length=0;length<bytes.size();++length)reject([&]{CinemaDecoder broken({bytes.begin(),bytes.begin()+length});while(!broken.done())broken.next();});
 }
 for(unsigned n=0;n<6;++n){auto bad=cinema();switch(n){case 0:bad[25]=24;break;case 1:bad[26]=1;break;case 2:word(bad,17,0);break;case 3:word(bad,28,0);break;case 4:bad[200]='Q';break;case 5:word(bad,201+8,4096,2);break;}reject([&]{CinemaDecoder broken(bad);while(!broken.done())broken.next();});}
 for(bool overshoot:{false,true}) {
  CinemaDecoder decoder(advanced_cinema(overshoot));decoder.next();check(decoder.done());
  const std::vector<unsigned> expected=overshoot?std::vector<unsigned>(9,1):std::vector<unsigned>({1,1,1,1,1,1,1,1,2,2,2,2,1,1,3,3});
  for(std::size_t k=0;k<expected.size();++k)check(decoder.dib()[k*16]==expected[k]*10);
 }
 // A reused index-0 frame must preserve the previous destination, then X must do so too.
 auto sequence=cinema();word(sequence,28,3);sequence.insert(sequence.begin()+200,16,0);
 const auto before=sequence.size();sequence.push_back('S');sequence.resize(before+1+47);word(sequence,before+1,5);word(sequence,before+5,5);word(sequence,before+9,1,2);word(sequence,before+11,4,2);word(sequence,before+13,32);word(sequence,before+17,2);
 BitWriter transparent;transparent.put(0,1);for(unsigned i=0;i<8;++i){transparent.put(0,3);transparent.put(0,1);}transparent.put(1,1);sequence.insert(sequence.end(),transparent.bytes.begin(),transparent.bytes.end());
 sequence.push_back('X');const auto drop=sequence.size();sequence.resize(drop+47);word(sequence,drop,0);
 CinemaDecoder sequence_decoder(sequence);sequence_decoder.next();const auto first_pixels=sequence_decoder.dib();sequence_decoder.next();check(sequence_decoder.dib()==first_pixels);check(sequence_decoder.next().kind==CinemaEvent::Dropped && sequence_decoder.dib()==first_pixels && sequence_decoder.done());
 auto d2=cinema();const auto book_start=248+Bytes(d2).u32(205);d2.resize(book_start);for(unsigned i=0;i<2;++i)d2.insert(d2.end(),{5,7,9,0});d2.insert(d2.end(),{0,0});word(d2,17,16);word(d2,201,d2.size()-248);word(d2,211,2,2);word(d2,213,16);CinemaDecoder two_pixels(d2);two_pixels.next();check(two_pixels.dib().size()==16*2*4 && two_pixels.dib()[0]==5 && two_pixels.dib()[4]==5);
 auto multi=cinema(true);multi.insert(multi.begin()+208,32,0);multi[27]=3;for(unsigned p:{208,224}){multi[p]=1;multi[p+1]=16;word(multi,p+2,22050,2);}for(char tag:{'A','B'}){multi.push_back(tag);const auto at=multi.size();word(multi,at,4);multi.insert(multi.end(),{3,0,4,0});}CinemaDecoder tracks(multi);unsigned track_mask=0;while(!tracks.done()){const auto event=tracks.next();if(event.kind==CinemaEvent::Audio)track_mask|=1U<<event.channel;}check(track_mask==7);
 // Empty sound chunks remain valid no-op events, including after the last image.
 auto empty_audio=cinema(true);empty_audio.insert(empty_audio.begin()+225,{'Z',0,0,0,0});empty_audio.insert(empty_audio.end(),{'Z',0,0,0,0});
 CinemaDecoder empties(empty_audio),baseline(cinema(true));std::vector<std::size_t> audio_sizes;unsigned image_count=0;
 while(!empties.done()){const auto event=empties.next();if(event.kind==CinemaEvent::Audio)audio_sizes.push_back(event.pcm.size());if(event.kind==CinemaEvent::Frame)++image_count;}
 while(!baseline.done())baseline.next();check(audio_sizes==std::vector<std::size_t>({4,0,0}));check(image_count==1);check(empties.dib()==baseline.dib());
 if(argc==3){const auto exe=read_file(argv[1]);const auto real=extract_a01_navigation(Bytes(exe));check(real.items.size()==4 && real.uses.size()==4);const auto& palm=real.items.back();const auto& offer=real.uses.front();
  for(bool first:{false,true}){InteractionState state(real);state.pickup(offer.item);if(first)state.pickup(palm.id);check(state.use(offer.id));if(!first)state.pickup(palm.id);check(state.variable(offer.variable)==offer.value);check(state.variable(palm.counter)==palm.value);
   const Checkpoint cp{palm.node,{0,0,85},state.history()};const auto restored=restore_interactions(real,decode_checkpoint(Bytes(encode_checkpoint(real,3,cp)),real,3));
   for(const auto& node:real.nodes){const auto input=read_file(resolve_scene_assets(argv[2],real).at(node.id));const auto base=decode_aqc(Bytes(input));const AqcAnimation animation(input);check(compose_items(animation,base,state.visibility(node.id)).rgba==compose_items(animation,base,restored.visibility(node.id)).rgba);}
  }
 }
 std::cout<<checks<<" story/media checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
