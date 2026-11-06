#include "jerusalem/scene_audio.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
namespace jerusalem {
double ambient_gain(unsigned volume,unsigned master) {
  if (volume>100 || master>100) throw ParseError("ambient volume exceeds 100");
  // Preserve the original f32 constant promoted to x87 precision and truncation.
  constexpr double percent=0.00999999977648258209228515625;
  const auto attenuation= -10000-static_cast<int>((master*percent)*(volume*percent)* -10000.0);
  return std::pow(10.0,attenuation/2000.0);
}
LoopPcm::LoopPcm(std::vector<std::uint8_t> data):data_(std::move(data)) {
  if (data_.size()>max_resource_bytes) throw ParseError("scene audio exceeds resource limit");
  if (Bytes(data_).is(8,"RIFF") && Bytes(data_).is(16,"WAVE")) data_=decode_game_audio(Bytes(data_));
  if (data_.size()>max_resource_bytes) throw ParseError("decoded scene audio exceeds resource limit");
  wave_=parse_wave(Bytes(data_));
  if (!wave_.data_size || wave_.data_size%frame_bytes()) throw ParseError("empty/unaligned scene audio");
}
std::vector<std::uint8_t> LoopPcm::read(std::size_t frames) {
  if (!frames || frames>65536) throw ParseError("invalid scene audio block length");
  std::vector<std::uint8_t> block(frames*frame_bytes());
  std::size_t written=0;
  while (written<block.size()) {
    const auto count=std::min(block.size()-written,wave_.data_size-cursor_);
    std::copy_n(data_.data()+wave_.data_offset+cursor_,count,block.data()+written);
    written+=count; cursor_+=count;
    if (cursor_==wave_.data_size) {cursor_=0;++wraps_;}
  }
  return block;
}
}
