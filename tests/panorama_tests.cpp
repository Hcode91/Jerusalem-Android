#include "jerusalem/panorama.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

using namespace jerusalem;
namespace {
int checks=0;
void check(bool value) { ++checks; if (!value) throw std::runtime_error("panorama check failed: "+std::to_string(checks)); }
void close(double actual,double expected) { check(std::abs(actual-expected)<1e-8); }
void rejects(const std::function<void()>& f) {
  bool rejected=false;
  try { f(); } catch (const ParseError&) { rejected=true; }
  check(rejected);
}
}
int main() {
  try {
    PanoramaGeometry g{2048,856,0,360,-75,75};
    auto p=panorama_point(g,{0,0,85},0,0,640,480);
    close(p.x,1024); close(p.y,428);
    p=panorama_point(g,{90,0,85},0,0,640,480); close(p.x,1536); close(p.y,428);
    p=panorama_point(g,{225,0,85},0,0,640,480); close(p.x,256);
    p=panorama_point(g,{0,0,85},1,0,640,480); close(p.x,1024+2048*42.5/360);
    p=panorama_point(g,{0,20,85},0,0,640,480); close(p.y,428+856*20.0/150);
    const auto bottom=panorama_point(g,{0,0,85},0,1,640,480);
    const auto top=panorama_point(g,{0,0,85},0,-1,640,480);
    close(bottom.y+top.y,856); check(bottom.y>428 && bottom.y<856);
    // On the longer dimension the original FOV is the full angular extent.
    p=panorama_point(g,{0,0,85},0,1,480,640); close(p.y,428+856*42.5/150);
    auto camera=constrain_camera(g,{-1,100,100},640,480);
    close(camera.yaw,359); close(camera.fov,87); check(camera.pitch>38 && camera.pitch<40);
    camera=constrain_camera(g,{1080,-100,5},640,480);
    close(camera.yaw,0); close(camera.fov,30); check(camera.pitch< -58 && camera.pitch> -60);
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{constrain_camera(g,{nan,0,85},640,480);});
    rejects([&]{constrain_camera(g,{0,nan,85},640,480);});
    rejects([&]{constrain_camera(g,{0,0,nan},640,480);});
    rejects([&]{constrain_camera(g,{},0,480);});
    rejects([&]{panorama_point(g,{},nan,0,640,480);});
    rejects([&]{panorama_point(g,{},0,2,640,480);});
    auto invalid=g; invalid.pitch_max=invalid.pitch_min;
    rejects([&]{constrain_camera(invalid,{},640,480);});
    invalid=g; invalid.yaw_max=180;
    rejects([&]{constrain_camera(invalid,{},640,480);});
    Bitmap source{8,4,{}};
    for (unsigned y=0;y<4;++y) for (unsigned x=0;x<8;++x)
      source.rgba.insert(source.rgba.end(),{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y),0,255});
    const PanoramaGeometry small{8,4,0,360,-75,75};
    auto frame=render_panorama(source,small,{0,0,85},1,1);
    check(frame.rgba==std::vector<std::uint8_t>({4,2,0,255}));
    frame=render_panorama(source,small,{180,0,85},1,1);
    check(frame.rgba==std::vector<std::uint8_t>({0,2,0,255}));
    // Avoid a texel boundary here: trig rounding can place an exact cardinal ray on either neighbour.
    frame=render_panorama(source,small,{280,0,85},1,1);
    check(frame.rgba==std::vector<std::uint8_t>({2,2,0,255}));
    rejects([&]{render_panorama(source,g,{},640,480);});
    rejects([&]{render_panorama(source,small,{},4096,4097);});
    source.rgba.pop_back(); rejects([&]{render_panorama(source,small,{},8,4);});
    {
      // The interactive grid renderer must stay within one source texel of the exact
      // mapping, including views across the 0/360 seam and at the pitch limits.
      const PanoramaGeometry big{2048,856,0,360,-75,75};
      Bitmap coded{2048,856,std::vector<std::uint8_t>(std::size_t(2048)*856*4)};
      for(unsigned y=0;y<856;++y)for(unsigned x=0;x<2048;++x){auto* p=&coded.rgba[(std::size_t(y)*2048+x)*4];p[0]=std::uint8_t(x&255);p[1]=std::uint8_t(x>>8);p[2]=std::uint8_t(y&255);p[3]=std::uint8_t(y>>8);}
      std::size_t samples=0,exact=0;
      for(const Camera cam:{Camera{0,0,85},Camera{359.5,10,60},Camera{180,-30,85},Camera{90,30,40},Camera{225,0,87}})
        for(const auto size:{std::pair<unsigned,unsigned>{640,300},{301,479}}){
          const auto a=render_panorama(coded,big,cam,size.first,size.second),b=render_panorama_fast(coded,big,cam,size.first,size.second);
          for(std::size_t i=0;i<a.rgba.size();i+=4){
            const int ax=a.rgba[i]|a.rgba[i+1]<<8,bx=b.rgba[i]|b.rgba[i+1]<<8,ay=a.rgba[i+2]|a.rgba[i+3]<<8,by=b.rgba[i+2]|b.rgba[i+3]<<8;
            const int dx=std::min(std::abs(ax-bx),2048-std::abs(ax-bx));
            check(dx<=1 && std::abs(ay-by)<=1);++samples;exact+=dx==0 && ay==by;
          }
        }
      check(exact*100>=samples*95);
      rejects([&]{render_panorama_fast(coded,big,{},640,300,0);});
    }
    std::cout << checks << " panorama checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
