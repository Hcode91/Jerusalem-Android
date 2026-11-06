#include "jerusalem/resources.hpp"
#include <algorithm>
#include <array>
#include <optional>

namespace jerusalem {
namespace {
class BitReader {
  Bytes data_;
  std::size_t position_=0;
 public:
  explicit BitReader(Bytes data):data_(data) {}
  std::size_t position() const { return position_; }
  std::uint32_t take(unsigned width) {
    if (width>16 || width>data_.size()*8-position_) throw ParseError("audio block ends inside a token");
    std::uint32_t value=0;
    for (unsigned i=0;i<width;++i,++position_)
      value=(value<<1)|((data_.u8(position_/8)>>(7-position_%8))&1U);
    return value;
  }
  void finish() {
    const auto remaining=data_.size()*8-position_;
    if (remaining>=8) throw ParseError("unexplained bytes after audio block tokens");
    if (take(static_cast<unsigned>(remaining))) throw ParseError("unsupported nonzero audio alignment bits");
  }
};
}

GameAudio parse_game_audio(Bytes b) {
  if (b.size()>max_resource_bytes) throw ParseError("compressed audio exceeds 64 MiB input limit");
  b.require(0,52);
  if (!b.is(8,"RIFF") || !b.is(16,"WAVEfmt ") || !b.is(44,"data") ||
      b.u32(24)!=16 || b.u16(28)!=1 || b.u16(42)!=16)
    throw ParseError("unsupported game audio: require fixed 44-byte 16-bit PCM WAVE header at offset 8");
  const auto count=b.u32(0), outer=b.u32(4), pcm=b.u32(48);
  const auto channels=b.u16(30), align=b.u16(40);
  const auto rate=b.u32(32);
  if (count==0 || count>10000 || outer>max_resource_bytes || pcm==0 ||
      (channels!=1 && channels!=2) || rate<1000 || rate>192000 ||
      align!=channels*2 || pcm%align || b.u32(36)!=rate*align ||
      std::uint64_t(b.u32(12))+8<std::uint64_t(pcm)+44)
    throw ParseError("invalid game audio geometry, length or block count");
  const auto riff=std::uint64_t(b.u32(12))+8;
  if ((channels==1 && (riff!=std::uint64_t(pcm)+44 || outer!=44+count*512 || outer<riff || outer-riff>=512)) ||
      (channels==2 && outer!=riff))
    throw ParseError("unsupported game audio output-length convention");
  GameAudio result{channels,rate,pcm,outer,{}};
  std::size_t position=52, total=0;
  for (std::uint32_t index=0;index<count;++index) {
    const auto stored=channels==1 ? std::uint32_t(b.u16(position)) : b.u32(position);
    const auto decoded=channels==1 ? 512U : b.u32(position+4);
    position+=channels==1 ? 2 : 8;
    b.require(position,stored);
    if (!stored || !decoded || decoded%align || decoded>outer || total>outer-decoded ||
        (channels==2 && std::uint64_t(decoded)>std::uint64_t(stored)*8))
      throw ParseError("invalid compressed audio block size");
    result.blocks.push_back({position,stored,decoded});
    position+=stored;
    total+=decoded;
  }
  if (position!=b.size() || total+44!=outer)
    throw ParseError("game audio block chain does not match file/output length");
  return result;
}

std::vector<std::uint8_t> decode_game_audio(Bytes b) {
  const auto info=parse_game_audio(b);
  std::vector<std::uint8_t> wave(std::size_t(info.pcm_size)+44);
  std::copy_n(b.data()+8,44,wave.begin());
  for (unsigned i=0;i<4;++i) wave[4+i]=static_cast<std::uint8_t>((info.pcm_size+36)>>(i*8));
  std::size_t written=44;
  const auto emit=[&](std::uint16_t sample) {
    if (written<wave.size()) {
      wave[written]=static_cast<std::uint8_t>(sample);
      wave[written+1]=static_cast<std::uint8_t>(sample>>8);
    }
    written+=2;
  };
  std::uint16_t predictor=0;
  std::optional<std::int32_t> delta;
  for (const auto& block:info.blocks) {
    BitReader bits(b.slice(block.payload_offset,block.stored_size));
    if (info.channels==1) {
      if (bits.take(3)) throw ParseError("unsupported nonzero mono audio prefix");
      for (unsigned i=0;i<256;++i) {
        if (bits.take(1)==0) {
          const auto code=bits.take(10);
          const auto magnitude=static_cast<std::int32_t>(code&511U)*64;
          delta=(code&512U) ? -magnitude : magnitude;
        }
        if (!delta) throw ParseError("mono audio delta reused before initialization");
        predictor=static_cast<std::uint16_t>(std::int32_t(predictor)+*delta);
        emit(predictor);
      }
    } else {
      std::array<std::uint16_t,64> cache{};
      std::array<std::size_t,64> used{};
      std::size_t replacement=0, selected=0;
      std::optional<std::uint16_t> previous;
      for (std::size_t i=0;i<block.decoded_size/2;++i) {
        std::uint16_t sample;
        if (bits.take(1)==0) {
          sample=static_cast<std::uint16_t>(bits.take(12)<<4);
          selected=replacement;
          cache[selected]=sample;
        } else {
          if (bits.take(1)==0) {
            selected=bits.take(6);
            sample=cache[selected];
          } else {
            if (!previous) throw ParseError("audio sample repeated before initialization");
            sample=*previous;
          }
          used[selected]=bits.position();
          if (selected==replacement)
            replacement=static_cast<std::size_t>(std::min_element(used.begin(),used.end())-used.begin());
        }
        emit(sample);
        previous=sample;
      }
    }
    bits.finish();
  }
  return wave;
}
} // namespace jerusalem
