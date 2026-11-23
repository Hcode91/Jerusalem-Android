#pragma once
#include "jerusalem/resources.hpp"
#include <algorithm>
#include <array>
#include <optional>

namespace jerusalem::detail {
class BitReader {
  Bytes data_;
  std::size_t position_=0;
 public:
  explicit BitReader(Bytes data):data_(data) {}
  std::size_t position() const { return position_; }
  std::uint32_t take(unsigned width) {
    if (width>16 || width>data_.size()*8-position_) throw ParseError("compressed block ends inside a token");
    std::uint32_t value=0;
    for (unsigned i=0;i<width;++i,++position_)
      value=(value<<1)|((data_.u8(position_/8)>>(7-position_%8))&1U);
    return value;
  }
  void finish() {
    const auto remaining=data_.size()*8-position_;
    if (remaining>=8) throw ParseError("unexplained bytes after compressed block tokens");
    if (take(static_cast<unsigned>(remaining))) throw ParseError("unsupported nonzero alignment bits");
  }
};

template<class Emit>
void decode_cache(Bytes payload,unsigned literal_width,unsigned shift,std::size_t count,Emit emit) {
  if (count>payload.size()*4) throw ParseError("decoded word count exceeds possible token count");
  BitReader bits(payload);
  std::array<std::uint16_t,64> cache{};
  std::array<std::size_t,64> used{};
  std::size_t replacement=0, selected=0;
  std::optional<std::uint16_t> previous;
  for (std::size_t i=0;i<count;++i) {
    std::uint16_t sample;
    if (bits.take(1)==0) {
      sample=static_cast<std::uint16_t>(bits.take(literal_width)<<shift);
      selected=replacement;
      cache[selected]=sample;
    } else {
      if (bits.take(1)==0) {
        selected=bits.take(6);
        sample=cache[selected];
      } else {
        if (!previous) throw ParseError("compressed word repeated before initialization");
        sample=*previous;
      }
      used[selected]=bits.position();
      if (selected==replacement)
        replacement=static_cast<std::size_t>(std::min_element(used.begin(),used.end())-used.begin());
    }
    emit(sample);
    previous=sample;
  }
  bits.finish();
}
}
