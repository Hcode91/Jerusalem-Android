#include "jerusalem/aqc_animation.hpp"
#include "codec_bits.hpp"
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstring>

namespace jerusalem {
std::vector<AqcChannel> parse_aqc_channels(Bytes b) {
  const auto base=parse_aqc(b);
  std::size_t position=base.static_end, total=0;
  std::vector<AqcChannel> channels;
  while (position<b.size()) {
    const auto count=b.u32(position), word=b.u32(position+4), last=b.u32(position+8);
    float timing;
    std::memcpy(&timing,&word,4);
    if (channels.size()>=256 || !count || count>1000 || total+count>10000 ||
        !std::isfinite(timing) || (timing!=0 && timing!=12) || last!=count-1 || (count==1)!=(timing==0))
      throw ParseError("unsupported AQC channel header at byte "+std::to_string(position));
    AqcChannel channel{timing,last,{}};
    position+=12;
    for (std::uint32_t i=0;i<count;++i) {
      b.require(position,56);
      if (b.u32(position+4)!=base.width || b.u32(position+8)!=base.height || b.u32(position+12)!=2 ||
          !std::equal(b.data()+16,b.data()+32,b.data()+position+16))
        throw ParseError("unsupported AQC frame header at byte "+std::to_string(position));
      const auto left=b.u32(position+32),right=b.u32(position+36),top=b.u32(position+40),bottom=b.u32(position+44);
      if (left>=right || right>base.width || top>=bottom || bottom>base.height || left%4 || (right-left)%4 ||
          b.u32(position+52)!=(right-left)*2 || b.u32(position+48)!=(right-left)*(bottom-top)*2)
        throw ParseError("unsupported AQC patch geometry at byte "+std::to_string(position));
      const auto size=b.u32(position);
      b.require(position+56,size);
      if (!size) throw ParseError("empty AQC frame payload at byte "+std::to_string(position));
      if (std::uint64_t(right-left)*(bottom-top)/4>std::uint64_t(size)*4)
        throw ParseError("AQC frame output exceeds possible token count");
      channel.frames.push_back({position,position+56,size,left,top,right-left,bottom-top});
      position+=56+size;
    }
    channels.push_back(std::move(channel));
    total+=count;
  }
  return channels;
}

AqcAnimation::AqcAnimation(std::vector<std::uint8_t> bytes):data_(std::move(bytes)) {
  const Bytes b(data_);
  channels_=parse_aqc_channels(b);
  const auto info=parse_aqc(b);
  info_=info;
  palette_.reserve(32400);
  detail::decode_cache(b.slice(info.palette_offset,info.palette_size),16,0,32400,
      [&](std::uint16_t pixel){palette_.push_back(pixel);});
}

Bitmap AqcAnimation::frame(std::size_t channel,std::size_t frame_index) const {
  if (channel>=channels_.size() || frame_index>=channels_[channel].frames.size())
    throw ParseError("AQC channel/frame selection is out of range");
  const auto& f=channels_[channel].frames[frame_index];
  Bitmap patch{f.width,f.height,{}};
  patch.rgba.reserve(std::size_t(f.width)*f.height*4);
  detail::decode_cache(Bytes(data_).slice(f.payload_offset,f.stored_size),13,0,std::size_t(f.width)*f.height/4,
      [&](std::uint16_t index){
        if (index>=8100) throw ParseError("AQC patch index exceeds codebook");
        for (unsigned i=0;i<4;++i) {
          const auto word=palette_[std::size_t(index)*4+i];
          patch.rgba.insert(patch.rgba.end(),{
              static_cast<std::uint8_t>((word>>8)&248),static_cast<std::uint8_t>((word>>3)&252),
              static_cast<std::uint8_t>((word<<3)&248),255});
        }
      });
  return patch;
}

void apply_aqc_patch(Bitmap& target,const Bitmap& patch,std::uint32_t left,std::uint32_t top) {
  if (!target.width || !target.height || !patch.width || !patch.height ||
      std::uint64_t(target.width)*target.height*4!=target.rgba.size() ||
      std::uint64_t(patch.width)*patch.height*4!=patch.rgba.size() ||
      left>target.width || top>target.height || patch.width>target.width-left || patch.height>target.height-top)
    throw ParseError("AQC patch does not fit the destination image");
  for (std::size_t y=0;y<patch.height;++y)
    std::copy_n(patch.rgba.data()+y*patch.width*4,std::size_t(patch.width)*4,
                target.rgba.data()+((std::size_t(top)+y)*target.width+left)*4);
}

std::size_t aqc_preview_frame(std::uint64_t elapsed,std::size_t count,unsigned fps) {
  if (!count || count>1000 || !fps || fps>60) throw ParseError("invalid AQC preview clock geometry");
  return static_cast<std::size_t>(((elapsed/1000)%count*fps+(elapsed%1000)*fps/1000)%count);
}

AqcPreview::AqcPreview(std::vector<std::uint8_t> bytes,const Bitmap& base):animation_(std::move(bytes)),base_(base) {
  if (base.width!=animation_.info().width || base.height!=animation_.info().height ||
      std::uint64_t(base.width)*base.height*4!=base.rgba.size())
    throw ParseError("invalid AQC preview base image");
}

bool AqcPreview::next() {
  const auto start=channel_ ? *channel_+1 : 0;
  channel_.reset(); frame_.reset(); elapsed_=0; reset_=true;
  for (std::size_t i=start;i<animation_.channels().size();++i) {
    if (animation_.channels()[i].frames.size()>1) { channel_=i; break; }
  }
  return channel_.has_value();
}

const AqcChannel* AqcPreview::selected() const {
  return channel_ ? &animation_.channels()[*channel_] : nullptr;
}

bool AqcPreview::update(Bitmap& target,std::uint64_t delta_ms) {
  if (!channel_) {
    if (!reset_) return false;
    target=base_; reset_=false; return true;
  }
  const auto count=selected()->frames.size();
  const auto period=std::uint64_t(count)*1000;
  elapsed_=(elapsed_+delta_ms%period)%period;
  const auto index=aqc_preview_frame(elapsed_,count);
  if (!reset_ && frame_==index) return false;
  const auto patch=animation_.frame(*channel_,index);
  const auto& info=selected()->frames[index];
  target=base_;
  apply_aqc_patch(target,patch,info.left,info.top);
  frame_=index; reset_=false;
  return true;
}
}
