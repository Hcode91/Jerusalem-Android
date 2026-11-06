#include "jerusalem/navigation.hpp"
#include "jerusalem/scene_audio.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
using namespace jerusalem;
namespace {
unsigned checks=0;
void check(bool value) { ++checks; if (!value) throw std::runtime_error("navigation assertion "+std::to_string(checks)); }
template<class F> void reject(F f) { bool failed=false; try { f(); } catch (const std::exception&) { failed=true; } check(failed); }
const std::string model="JERUSALEM_A01_NAV_V1\nnode 1 SYNTH_A 2\nnode 2 SYNTH_B 2\nexit 1 0 2 0 1 0 -750 3600 750 53 0 0 0 85\nexit 2 0 1 0 0 0 -750 3600 750 53 0 0 0 85\nexit 2 1 99 1 1 0 -750 3600 750 53 0 0 0 85\n";
}
int main(int argc,char** argv) {
 try {
  auto g=parse_navigation(model); check(write_navigation(g)==model);
  check(scene_node(g,1).channels==2); reject([&]{scene_node(g,9);});
  check(navigation_hit(g,1,0,-750)==0); check(!navigation_hit(g,1,3600,0));
  check(!navigation_hit(g,1,1,750)); check(!navigation_hit(g,1,-1,0));
  check(navigation_hit(g,2,1800,0)==2); check(!exit_contains(g.exits[1],1800,0));
  for (const auto& bad:{std::string(""),std::string("JERUSALEM_A01_NAV_V1\n"),model+"unknown\n",model+"node 1 THIRD 0\n",model+"node 3 SYNTH_A 0\n",model+"node 3 ../PATH 0\n",model+"node -1 BAD 0\n",model+"node 3 BAD 257\n",model+"node 3 BAD 0 junk\n",std::string(131073,'x')}) reject([&]{parse_navigation(bad);});
  for (unsigned test=0;test<12;++test) {
    auto bad=g; auto& e=bad.exits[0];
    switch(test) {
     case 0: e.target=3; break; case 1:e.source=3;break; case 2:e.left=e.right;break;
     case 3:e.right=3601;break;case 4:e.top= -901;break;case 5:e.bottom=e.top;break;
     case 6:e.arrival.yaw=std::numeric_limits<double>::quiet_NaN();break;
     case 7:e.arrival.fov=88;break;case 8:e.slot=64;break;
     case 9:bad.exits.push_back(e);break;case 10:e.target=0;break;case 11:e.arrival.pitch=91;break;
    }
    reject([&]{validate_navigation(bad);});
  }
  for (const auto& row:{"exit 1 1 2 2 1 0 -750 3600 750 53 0 0 0 85", "exit 1 1 2 0 2 0 -750 3600 750 53 0 0 0 85", "exit 1 1 2 0 1 0 -750 3600 750 53 0 nan 0 85"}) reject([&]{parse_navigation(model+row+"\n");});
  std::vector<std::uint8_t> code{0x56,0x8b,0xf1,0x6a,0xff,0x68,0x78,0x56,0x34,0x12,0x8b,0xce,0xe8,0,0,0,0};
  const auto calls=parse_setup_calls(Bytes(code),0x1000);
  check(calls.size()==1 && calls[0].target==0x1011 && calls[0].arguments==std::vector<std::uint32_t>({0x12345678,0xffffffff}));
  for (std::size_t i=0;i<code.size();++i) if (i!=3) reject([&]{parse_setup_calls(Bytes(code).slice(0,i),0x1000);});
  auto badcode=code; badcode[12]=0x90; reject([&]{parse_setup_calls(Bytes(badcode),0x1000);});
  reject([&]{parse_setup_calls(Bytes(code),0xfffffff0);});
  reject([&]{extract_a01_navigation(Bytes(code));});
  std::vector<std::uint8_t> wrong(811008); reject([&]{extract_a01_navigation(Bytes(wrong));});
  const PanoramaGeometry geometry{2048,856,0,360,-75,75};
  check(hotspot_point(geometry,{2048,428}).x==0);
  check(hotspot_point(geometry,{1024,428}).x==1800);
  check(hotspot_point(geometry,{1024,428}).y==0);
  reject([&]{hotspot_point(geometry,{INFINITY,0});});
  for (double yaw:{0.0,45.0,180.0,359.0}) for (double pitch:{-15.0,0.0,15.0}) {
    const Camera camera{yaw,pitch,85};
    const auto point=panorama_point(geometry,camera,0,0,960,672);
    const auto angular=hotspot_point(geometry,point);
    const auto screen=hotspot_screen(geometry,camera,angular.x,angular.y,960,672);
    check(screen.has_value() && std::abs(screen->x)<0.01 && std::abs(screen->y)<0.02);
  }
  check(!hotspot_screen(geometry,{0,0,85},0,0,960,672));
  check(!hotspot_screen(geometry,{0,0,85},2700,0,960,672));
  reject([&]{hotspot_screen(geometry,{0,0,85},NAN,0,960,672);});
  const auto root=std::filesystem::temp_directory_path()/"jerusalem-navigation-regression";
  struct Cleanup { std::filesystem::path p; ~Cleanup(){std::error_code error;std::filesystem::remove_all(p,error);} } cleanup{root};
  if (!std::filesystem::create_directory(root)) throw std::runtime_error("test directory exists");
  std::ofstream(root/"synth_a.AqC").put('a'); reject([&]{resolve_scene_assets(root,g);});
  std::ofstream(root/"SYNTH_B.aqc").put('b'); check(resolve_scene_assets(root,g).size()==2);
  std::filesystem::create_directory(root/"nested"); std::ofstream(root/"nested/SYNTH_A.aqc").put('a');
  reject([&]{resolve_scene_assets(root,g);}); std::filesystem::remove(root/"nested/SYNTH_A.aqc");
  std::filesystem::create_symlink(root/"synth_a.AqC",root/"nested/SYNTH_A.aqc"); check(resolve_scene_assets(root,g).size()==2);
  g.ambient={{1,7,55,"WIND"},{2,7,80,"WIND"},{1,8,80,"MUSIC"}};
  const auto v2=write_navigation(g); check(v2.find("NAV_V2")!=std::string::npos);
  check(write_navigation(parse_navigation(v2))==v2);
  for (unsigned test=0;test<8;++test) {
    auto bad=g;
    switch(test) {
     case 0:bad.ambient[0].node=99;break;case 1:bad.ambient[0].sound=0;break;
     case 2:bad.ambient[0].volume=101;break;case 3:bad.ambient[0].stem="../WIND";break;
     case 4:bad.ambient[1].stem="OTHER";break;case 5:bad.ambient.push_back(bad.ambient[0]);break;
     case 6:bad.ambient.push_back({1,9,50,"THIRD"});bad.ambient.push_back({1,10,50,"FOURTH"});break;
     case 7:bad.ambient[2].stem="WIND";break;
    }
    reject([&]{validate_navigation(bad);});
  }
  reject([&]{parse_navigation(model+"ambient 1 7 55 WIND\n");});
  reject([&]{parse_navigation(v2+"ambient 2 9 100 EXTRA junk\n");});
  check(ambient_gain(100)>0.998 && ambient_gain(100)<=1);
  // Golden attenuations captured from the original x87 routine, not linear gain.
  for (const auto& pair:{std::pair{50U,-5001},std::pair{55U,-4501},std::pair{65U,-3501},std::pair{70U,-3001},std::pair{80U,-2001},std::pair{100U,-1}})
    check(std::abs(ambient_gain(pair.first)-std::pow(10,pair.second/2000.0))<1e-12);
  check(ambient_gain(0)==0.00001); check(ambient_gain(80,50)<ambient_gain(80));
  reject([&]{ambient_gain(101);});reject([&]{ambient_gain(80,101);});
  std::ofstream(root/"WIND.wav").put('w'); std::ofstream(root/"WIND.wac").put('c');
  check(resolve_ambient_asset(root/"synth_a.AqC","WIND").extension()==".wac");
  std::ofstream(root/"WIND.was").put('s');check(resolve_ambient_asset(root/"synth_a.AqC","WIND").extension()==".was");
  reject([&]{resolve_ambient_asset(root/"synth_a.AqC","MISSING");});
  reject([&]{resolve_ambient_asset(root/"synth_a.AqC","../BAD");});
  std::ofstream(root/"nested/wind.was").put('d'); reject([&]{resolve_ambient_asset(root/"synth_a.AqC","WIND");});
  std::filesystem::remove(root/"nested/wind.was");
  std::filesystem::create_directory(root/"NODE");
  check(resolve_ambient_asset(root/"NODE/synth_a.AqC","WIND").extension()==".was");
  std::vector<std::uint8_t> wave(48);
  const auto put=[&](std::size_t offset,std::uint32_t value,unsigned bytes=4){for(unsigned i=0;i<bytes;++i)wave[offset+i]=std::uint8_t(value>>(8*i));};
  const auto tag=[&](std::size_t offset,const std::string& text){std::copy(text.begin(),text.end(),wave.begin()+offset);};
  tag(0,"RIFF");put(4,40);tag(8,"WAVEfmt ");put(16,16);put(20,1,2);put(22,1,2);put(24,8000);put(28,8000);put(32,1,2);put(34,8,2);tag(36,"data");put(40,4);
  wave[44]=1;wave[45]=2;wave[46]=3;wave[47]=4;
  LoopPcm loop(wave);check(loop.frame_bytes()==1 && loop.wave().rate==8000);
  check(loop.read(3)==std::vector<std::uint8_t>({1,2,3}));check(loop.wraps()==0);
  check(loop.read(6)==std::vector<std::uint8_t>({4,1,2,3,4,1}));check(loop.wraps()==2);
  check(loop.read(1)==std::vector<std::uint8_t>({2}));
  reject([&]{loop.read(0);});reject([&]{loop.read(65537);});
  put(22,2,2);put(28,32000);put(32,4,2);put(34,16,2);
  LoopPcm stereo(wave);check(stereo.frame_bytes()==4);
  check(stereo.read(2)==std::vector<std::uint8_t>({1,2,3,4,1,2,3,4}));check(stereo.wraps()==2);
  auto broken=wave;broken.pop_back();reject([&]{LoopPcm invalid(broken);});
  // Optional complete user-supplied audio validation; no originals enter CI.
  for (int i=1;i<argc;++i) {
    LoopPcm original(read_file(argv[i])); const auto frames=original.wave().data_size/original.frame_bytes();
    if (frames<13) throw std::runtime_error("private audio fixture too short");
    const auto first=original.read(13);
    for (auto remaining=frames-13;remaining;) {const auto amount=std::min<std::size_t>(65536,remaining); original.read(amount); remaining-=amount;}
    check(original.wraps()==1);check(original.read(13)==first);
    std::cout << "Original scene sound validated: " << std::filesystem::path(argv[i]).filename().string() << " frames=" << frames << '\n';
  }
  std::cout << checks << " navigation checks passed\n";
  return 0;
 } catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
