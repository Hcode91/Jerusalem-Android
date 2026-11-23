#include "jerusalem/panorama.hpp"
#include "jerusalem/navigation.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace jerusalem {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr double radians=pi/180.0;
void validate(const PanoramaGeometry& g,unsigned width,unsigned height) {
  if (!width || !height || width>4096 || height>4096 || std::uint64_t(width)*height*4>max_resource_bytes ||
      !g.width || !g.height || g.width>8192 || g.height>8192 ||
      !std::isfinite(g.yaw_min) || !std::isfinite(g.yaw_max) ||
      !std::isfinite(g.pitch_min) || !std::isfinite(g.pitch_max) ||
      g.yaw_min!=0 || g.yaw_max!=360 || g.pitch_min>=g.pitch_max ||
      g.pitch_min< -90 || g.pitch_max>90)
    throw ParseError("unsupported panorama geometry or output dimensions");
}
void validate_camera(const Camera& c) {
  if (!std::isfinite(c.yaw) || !std::isfinite(c.pitch) || !std::isfinite(c.fov))
    throw ParseError("nonfinite panorama camera");
}
class Projection {
  const PanoramaGeometry& g_;
  double sy_,cy_,sp_,cp_,sx_,sy_screen_,z_;
 public:
  Projection(const PanoramaGeometry& g,const Camera& c,unsigned width,unsigned height):g_(g) {
    const auto half=c.fov*radians/2;
    sy_=std::sin(c.yaw*radians); cy_=std::cos(c.yaw*radians);
    sp_=std::sin(c.pitch*radians); cp_=std::cos(c.pitch*radians);
    sx_=std::sin(half)*width/std::max(width,height);
    sy_screen_=std::sin(half)*height/std::max(width,height);
    z_=std::cos(half);
  }
  TexturePoint point(double nx,double ny) const {
    // Right, down and forward vectors follow the original world-up basis construction.
    const double rx=nx*sx_, ry=ny*sy_screen_;
    const double x=cy_*rx-sy_*sp_*ry+sy_*cp_*z_;
    const double y=cp_*ry+sp_*z_;
    const double z=-sy_*rx-cy_*sp_*ry+cy_*cp_*z_;
    const double latitude=std::asin(std::clamp(y/std::sqrt(x*x+y*y+z*z),-1.0,1.0))/radians;
    const double longitude=std::atan2(x,z)/radians;
    return {(longitude+180)/360*g_.width,
            (latitude-(g_.pitch_min+g_.pitch_max)/2)/(g_.pitch_max-g_.pitch_min)*g_.height+g_.height/2.0};
  }
};
}

PanoramaGeometry panorama_geometry(Bytes aqc) {
  const auto info=parse_aqc(aqc);
  double fields[4];
  for (unsigned i=0;i<4;++i) {
    const auto word=aqc.u32(16+i*4);
    float value;
    std::memcpy(&value,&word,4);
    fields[i]=value;
  }
  PanoramaGeometry g{info.width,info.height,fields[0],fields[1],fields[2],fields[3]};
  validate(g,1,1);
  return g;
}

Camera constrain_camera(const PanoramaGeometry& g,Camera c,unsigned width,unsigned height) {
  validate(g,width,height); validate_camera(c);
  c.yaw=std::fmod(c.yaw,360);
  if (c.yaw<0) c.yaw+=360;
  c.fov=std::clamp(c.fov,30.0,87.0);
  // Match the original conservative pitch limit, including its 5-degree margin.
  const auto half_vertical=std::asin(std::sin(c.fov*radians/2)*height/std::max(width,height))/radians;
  const auto margin=(g.pitch_max-g.pitch_min)/2-half_vertical-5;
  if (margin<0) throw ParseError("panorama angular coverage is too small for this camera");
  const auto center=(g.pitch_min+g.pitch_max)/2;
  c.pitch=std::clamp(c.pitch,center-margin,center+margin);
  return c;
}

TexturePoint panorama_point(const PanoramaGeometry& g,const Camera& c,double nx,double ny,unsigned width,unsigned height) {
  validate(g,width,height); validate_camera(c);
  if (!std::isfinite(nx) || !std::isfinite(ny) || nx< -1 || nx>1 || ny< -1 || ny>1)
    throw ParseError("panorama sample is outside the viewport");
  return Projection(g,c,width,height).point(nx,ny);
}

std::optional<TexturePoint> hotspot_screen(const PanoramaGeometry& g,const Camera& c,double yaw,double pitch,unsigned width,unsigned height) {
  validate(g,width,height); validate_camera(c);
  if (c.fov<30 || c.fov>87) throw ParseError("unsupported hotspot camera FOV");
  if (!std::isfinite(yaw) || !std::isfinite(pitch) || yaw<0 || yaw>3600 || pitch< -900 || pitch>900)
    throw ParseError("invalid angular hotspot centre");
  const auto longitude=(yaw/10-180)*radians,latitude=pitch/10*radians;
  const auto x=std::sin(longitude)*std::cos(latitude),y=std::sin(latitude),z=std::cos(longitude)*std::cos(latitude);
  const auto sy=std::sin(c.yaw*radians),cy=std::cos(c.yaw*radians),sp=std::sin(c.pitch*radians),cp=std::cos(c.pitch*radians);
  const auto forward=sy*cp*x+sp*y+cy*cp*z;
  if (forward<=0) return std::nullopt;
  const auto half=c.fov*radians/2;
  const auto nx=(cy*x-sy*z)/forward/std::tan(half)*std::max(width,height)/width;
  const auto ny=(-sy*sp*x+cp*y-cy*sp*z)/forward/std::tan(half)*std::max(width,height)/height;
  if (std::abs(nx)>1 || std::abs(ny)>1) return std::nullopt;
  return TexturePoint{nx,ny};
}

Bitmap render_panorama(const Bitmap& source,const PanoramaGeometry& g,Camera c,unsigned width,unsigned height) {
  c=constrain_camera(g,c,width,height);
  if (source.width!=g.width || source.height!=g.height ||
      std::uint64_t(source.width)*source.height*4!=source.rgba.size())
    throw ParseError("panorama source buffer does not match geometry");
  Bitmap out{width,height,{}};
  out.rgba.resize(std::size_t(width)*height*4);
  Projection projection(g,c,width,height);
  for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
    const auto p=projection.point(2*(x+0.5)/width-1,2*(y+0.5)/height-1);
    // Wrap longitude at the seam. Clamp latitude before nearest-neighbour sampling.
    const auto px=static_cast<std::size_t>(std::floor(std::clamp(p.x,0.0,double(g.width))))%g.width;
    const auto py=static_cast<std::size_t>(std::floor(std::clamp(p.y,0.0,double(g.height-1))));
    const auto src=(py*g.width+px)*4, dst=(std::size_t(y)*width+x)*4;
    std::copy_n(source.rgba.data()+src,4,out.rgba.data()+dst);
  }
  return out;
}

// Same projection, but the exact (trigonometric) mapping is evaluated only on a grid
// every `step` pixels and interpolated between grid points, with longitude unwrapped
// across the seam. On a phone this is the difference between ~20 ms and ~2 ms per
// 640-pixel view; tests bound its deviation from render_panorama.
Bitmap render_panorama_fast(const Bitmap& source,const PanoramaGeometry& g,Camera c,unsigned width,unsigned height,unsigned step) {
  if(step<1 || step>32)throw ParseError("unsupported panorama grid step");
  c=constrain_camera(g,c,width,height);
  if (source.width!=g.width || source.height!=g.height ||
      std::uint64_t(source.width)*source.height*4!=source.rgba.size())
    throw ParseError("panorama source buffer does not match geometry");
  Bitmap out{width,height,{}};
  out.rgba.resize(std::size_t(width)*height*4);
  const Projection projection(g,c,width,height);
  const unsigned gw=(width+step-1)/step+1,gh=(height+step-1)/step+1;
  std::vector<TexturePoint> grid(std::size_t(gw)*gh);
  const double full=g.width;
  for(unsigned j=0;j<gh;++j)for(unsigned i=0;i<gw;++i){
    // Grid points sit on pixel centres; the last row/column may lie past the edge.
    const double x=std::min<double>(i*step,width-1)+0.5,y=std::min<double>(j*step,height-1)+0.5;
    grid[std::size_t(j)*gw+i]=projection.point(2*x/width-1,2*y/height-1);
  }
  const auto* src=source.rgba.data();auto* dst=out.rgba.data();
  for(unsigned j=0;j+1<gh;++j){
    const unsigned y0=j*step,y1=std::min(height,y0+step);
    if(y0>=height)break;
    const double ys0=y0,ys1=std::max<double>(std::min(y0+step,height-1),ys0+1e-9);
    for(unsigned i=0;i+1<gw;++i){
      const unsigned x0=i*step,x1=std::min(width,x0+step);
      if(x0>=width)break;
      const double xs0=x0,xs1=std::max<double>(std::min(x0+step,width-1),xs0+1e-9);
      auto a=grid[std::size_t(j)*gw+i],b=grid[std::size_t(j)*gw+i+1],c0=grid[std::size_t(j+1)*gw+i],d=grid[std::size_t(j+1)*gw+i+1];
      // Unwrap longitudes relative to the cell's first corner.
      for(auto* p:{&b,&c0,&d}){if(p->x-a.x>full/2)p->x-=full;else if(a.x-p->x>full/2)p->x+=full;}
      for(unsigned y=y0;y<y1;++y){
        const double ty=(double(y)-ys0)/(ys1-ys0);
        const double lx=a.x+(c0.x-a.x)*ty,ly=a.y+(c0.y-a.y)*ty,rx=b.x+(d.x-b.x)*ty,ry=b.y+(d.y-b.y)*ty;
        // Step linearly along the row in 16.16 fixed point; longitudes stay within one
        // texture width of the cell corner, so a single wrap adjustment suffices.
        const double inv=1.0/(xs1-xs0);
        const double px0=lx+(rx-lx)*(double(x0)-xs0)*inv,py0=ly+(ry-ly)*(double(x0)-xs0)*inv;
        std::int64_t fx=static_cast<std::int64_t>(std::floor(px0*65536.0)),fy=static_cast<std::int64_t>(std::floor(py0*65536.0));
        const std::int64_t dx=static_cast<std::int64_t>((rx-lx)*inv*65536.0),dy=static_cast<std::int64_t>((ry-ly)*inv*65536.0);
        const std::int64_t w=g.width,maxy=g.height-1;
        auto* out_row=dst+(std::size_t(y)*width+x0)*4;
        for(unsigned x=x0;x<x1;++x,fx+=dx,fy+=dy,out_row+=4){
          std::int64_t sx=fx>>16,sy=fy>>16;
          if(sx<0)sx+=w*((-sx+w-1)/w);
          if(sx>=w)sx%=w;
          sy=sy<0?0:sy>maxy?maxy:sy;
          std::memcpy(out_row,src+(std::size_t(sy)*g.width+std::size_t(sx))*4,4);
        }
      }
    }
  }
  return out;
}
}
