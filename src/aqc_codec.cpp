#include "jerusalem/resources.hpp"
#include "codec_bits.hpp"
#include <cmath>
#include <cstring>

namespace jerusalem {
AqcInfo parse_aqc(Bytes b) {
  if (b.size()>max_resource_bytes) throw ParseError("AQC exceeds 64 MiB input limit");
  b.require(0,60);
  const auto width=b.u32(4), height=b.u32(8);
  if (!width || width>8192 || width%4 || !height || height>8192 || b.u32(12)!=2 ||
      std::uint64_t(width)*height*4>max_resource_bytes)
    throw ParseError("unsupported AQC static image geometry");
  for (unsigned offset=16;offset<32;offset+=4) {
    const auto word=b.u32(offset);
    float angle;
    static_assert(sizeof(angle)==sizeof(word),"requires 32-bit floats");
    std::memcpy(&angle,&word,sizeof(angle));
    if (!std::isfinite(angle)) throw ParseError("invalid AQC angle field");
  }
  if (b.u32(32)!=0 || b.u32(36)!=width || b.u32(40)!=0 || b.u32(44)!=height ||
      b.u32(48)!=width*height*2 || b.u32(52)!=width*2)
    throw ParseError("unsupported AQC static header variant");
  const auto primary=b.u32(0);
  b.require(56,primary);
  const auto secondary_offset=std::size_t(56)+primary;
  const auto secondary=b.u32(secondary_offset);
  b.require(secondary_offset+4,secondary);
  if (!primary || !secondary) throw ParseError("empty AQC compressed stream");
  const auto end=secondary_offset+4+secondary;
  return {width,height,56,primary,secondary_offset+4,secondary,end,b.size()-end};
}

Bitmap decode_aqc(Bytes b) {
  const auto info=parse_aqc(b);
  std::vector<std::uint16_t> indices, palette;
  const auto count=std::size_t(info.width)*info.height/4;
  indices.reserve(count);
  palette.reserve(8100*4);
  detail::decode_cache(b.slice(info.indices_offset,info.indices_size),13,0,count,
      [&](std::uint16_t word){
        if (word>=8100) throw ParseError("AQC vector index exceeds verified codebook");
        indices.push_back(word);
      });
  detail::decode_cache(b.slice(info.palette_offset,info.palette_size),16,0,8100*4,
      [&](std::uint16_t word){palette.push_back(word);});
  Bitmap image{info.width,info.height,{}};
  image.rgba.reserve(std::size_t(info.width)*info.height*4);
  for (auto index:indices) for (unsigned component=0;component<4;++component) {
    const auto pixel=palette[std::size_t(index)*4+component];
    image.rgba.push_back(static_cast<std::uint8_t>((pixel>>8)&248));
    image.rgba.push_back(static_cast<std::uint8_t>((pixel>>3)&252));
    image.rgba.push_back(static_cast<std::uint8_t>((pixel<<3)&248));
    image.rgba.push_back(255);
  }
  return image;
}
}
