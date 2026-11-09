#pragma once
#include "jerusalem/resources.hpp"

namespace jerusalem {
struct PanoramaGeometry {
  std::uint32_t width, height;
  double yaw_min, yaw_max, pitch_min, pitch_max;
};
// Constructor defaults observed in the original. Supported A01 transition overrides are extracted separately.
struct Camera { double yaw=225, pitch=0, fov=85; };
struct TexturePoint { double x, y; };
PanoramaGeometry panorama_geometry(Bytes aqc);
Camera constrain_camera(const PanoramaGeometry& geometry,Camera camera,unsigned width,unsigned height);
TexturePoint panorama_point(const PanoramaGeometry& geometry,const Camera& camera,
                            double normalized_x,double normalized_y,unsigned width,unsigned height);
Bitmap render_panorama(const Bitmap& source,const PanoramaGeometry& geometry,Camera camera,
                       unsigned width,unsigned height);
// Faster approximation for interactive views: exact mapping every `step` pixels, interpolated between.
Bitmap render_panorama_fast(const Bitmap& source,const PanoramaGeometry& geometry,Camera camera,unsigned width,unsigned height,unsigned step=8);
}
