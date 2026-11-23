#pragma once
#include "jerusalem/resources.hpp"
#include <optional>

namespace jerusalem {
struct AqcFrame {
  std::size_t header_offset, payload_offset, stored_size;
  std::uint32_t left, top, width, height;
};
struct AqcChannel {
  float timing_value;
  std::uint32_t declared_last_frame;
  std::vector<AqcFrame> frames;
};
std::vector<AqcChannel> parse_aqc_channels(Bytes bytes);
class AqcAnimation {
  std::vector<std::uint8_t> data_;
  std::vector<AqcChannel> channels_;
  std::vector<std::uint16_t> palette_;
  AqcInfo info_{};
 public:
  explicit AqcAnimation(std::vector<std::uint8_t> bytes);
  const std::vector<AqcChannel>& channels() const { return channels_; }
  const AqcInfo& info() const { return info_; }
  Bitmap frame(std::size_t channel,std::size_t frame) const;
};
void apply_aqc_patch(Bitmap& target,const Bitmap& patch,std::uint32_t left,std::uint32_t top);
// Inspector-only playback policy. Game script timing/activation is not implemented.
std::size_t aqc_preview_frame(std::uint64_t elapsed_ms,std::size_t frame_count,unsigned fps=12);
class AqcPreview {
  AqcAnimation animation_;
  Bitmap base_;
  std::optional<std::size_t> channel_, frame_;
  std::uint64_t elapsed_=0;
  bool reset_=false;
 public:
  AqcPreview(std::vector<std::uint8_t> bytes,const Bitmap& base);
  bool next();
  bool update(Bitmap& target,std::uint64_t delta_ms);
  std::optional<std::size_t> channel() const { return channel_; }
  std::optional<std::size_t> frame() const { return frame_; }
  const AqcChannel* selected() const;
};
}
