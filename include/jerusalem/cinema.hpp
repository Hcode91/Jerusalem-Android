#pragma once
#include "jerusalem/resources.hpp"
#include <array>
#include <map>
namespace jerusalem {
struct CinemaSound { std::uint16_t channels,bits; std::uint32_t rate; };
struct CinemaInfo { std::uint32_t width,height,frames,rate_word,duration_ticks; std::vector<CinemaSound> sounds; };
struct CinemaEvent { enum Kind {Codebook,Audio,Frame,Dropped} kind; std::uint32_t channel=0; std::vector<std::uint8_t> pcm; };
// Sequential, bounded progressive CNM decoder. DLL timing: fps=rate_word/100; duration_ms=duration_ticks*10.
class CinemaDecoder {
  std::vector<std::uint8_t> bytes_,surface_;
  std::vector<std::uint8_t> coverage_;
  CinemaInfo info_{};
  std::size_t position_=0,initial_position_=0,frame_count_=0;
  std::uint64_t decoded_pixels_=0;
  std::vector<std::array<std::uint8_t,16>> book_;
  std::array<std::uint16_t,4> candidates_{};
  void codebook(Bytes bytes,unsigned d,unsigned n);
  void frame(Bytes header,Bytes payload);
 public:
  explicit CinemaDecoder(std::vector<std::uint8_t> bytes);
  const CinemaInfo& info() const {return info_;}
  bool done() const {return position_==bytes_.size();}
  bool pristine() const {return position_==initial_position_;}
  CinemaEvent next();
  Bitmap image() const;
  // Alpha describes vectors actually written, not a guessed black colour key.
  Bitmap sprite() const;
  Bitmap alpha_image() const;
  const std::vector<std::uint8_t>& dib() const {return surface_;}
};
struct CinemaSeek {std::uint64_t frames=0,position_ms=0;std::vector<std::uint8_t> pcm;};
// Replay a fresh decoder to its saved presentation time. Returned PCM starts at
// that time, including the remaining portion of a preloaded soundtrack chunk.
CinemaSeek seek_cinema(CinemaDecoder& decoder,std::uint64_t offset_ms);
}
