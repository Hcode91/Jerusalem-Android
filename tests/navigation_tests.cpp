#include "jerusalem/navigation.hpp"
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
int main() {
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
  std::cout << checks << " navigation checks passed\n";
  return 0;
 } catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
