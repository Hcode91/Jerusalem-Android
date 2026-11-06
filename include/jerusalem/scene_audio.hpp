#pragma once
#include "jerusalem/resources.hpp"
namespace jerusalem {
// Original DirectSound attenuation with a native 100% category master.
double ambient_gain(unsigned volume,unsigned master=100);
class LoopPcm {
  std::vector<std::uint8_t> data_;
  Wave wave_{};
  std::size_t cursor_=0;
  std::uint64_t wraps_=0;
 public:
  explicit LoopPcm(std::vector<std::uint8_t> data);
  const Wave& wave() const { return wave_; }
  std::size_t frame_bytes() const { return wave_.channels*(wave_.bits/8); }
  std::size_t storage_bytes() const { return data_.size(); }
  std::uint64_t wraps() const { return wraps_; }
  std::vector<std::uint8_t> read(std::size_t frames);
};
}
