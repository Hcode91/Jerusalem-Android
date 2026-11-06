#include "jerusalem/cinema.hpp"
#include <algorithm>
#include <utility>
namespace jerusalem {
namespace {
class Bits {
  Bytes bytes_;std::size_t bit_=0;std::string context_;
 public:
  explicit Bits(Bytes bytes,std::string context):bytes_(bytes),context_(std::move(context)){}
  std::uint32_t peek(unsigned n) const {
    if(n>16 || bit_>bytes_.size()*8+512 || n>bytes_.size()*8+512-bit_)throw ParseError("truncated CNM "+context_+" bitstream at bit "+std::to_string(bit_)+" of "+std::to_string(bytes_.size()*8));
    std::uint32_t value=0;for(unsigned i=0;i<n;++i)if((bit_+i)/8<bytes_.size())value|=((bytes_.u8((bit_+i)/8)>>((bit_+i)%8))&1U)<<i;return value;
  }
  std::uint32_t take(unsigned n){const auto value=peek(n);bit_+=n;return value;}
};
constexpr std::size_t max_pixels=4096*4096,max_frames=100000;
}
CinemaDecoder::CinemaDecoder(std::vector<std::uint8_t> bytes):bytes_(std::move(bytes)) {
  const Bytes b(bytes_);b.require(0,192);
  if(bytes_.size()>max_resource_bytes || !b.is(0,std::string("CNM UNR\0",8)))throw ParseError("not a supported CNM stream");
  const auto w=b.i32(17),h=b.i32(21);
  if(w<=0 || h<=0 || w>4096 || h>4096 || std::uint64_t(w)*h>max_pixels || b.u8(25)!=32 || b.u8(26)!=0 || b.u8(27)>3)throw ParseError("unsupported CNM geometry/bpp/interlaced variant");
  info_.width=static_cast<unsigned>(w);info_.height=static_cast<unsigned>(h);info_.frames=b.u32(28);info_.rate_word=b.u32(12);info_.duration_ticks=b.u32(36);
  if(!info_.frames || info_.frames>max_frames)throw ParseError("invalid CNM frame count");
  position_=192;
  // The DLL passes sound-header byte 0 plus one to its audio initializer.
  for(unsigned i=0;i<b.u8(27);++i){b.require(position_,16);CinemaSound s{static_cast<std::uint16_t>(b.u8(position_)+1U),b.u8(position_+1),b.u16(position_+2)};if((s.channels!=1 && s.channels!=2) || s.bits!=16 || s.rate<8000 || s.rate>48000)throw ParseError("unsupported CNM PCM configuration");info_.sounds.push_back(s);position_+=16;}
  b.require(position_,std::size_t(info_.frames)*8);position_+=std::size_t(info_.frames)*8;
  if(position_>=bytes_.size())throw ParseError("CNM chunk stream is missing");
  surface_.resize(std::size_t(w)*h*4,0);
}
void CinemaDecoder::codebook(Bytes data,unsigned d,unsigned n) {
  if((d!=2 && d!=4) || !n || n>4095)throw ParseError("unsupported CNM codebook dimensions");
  data.require(0,d*4);std::vector<std::array<std::uint8_t,16>> book(n+1);
  std::copy(data.data(),data.data()+d*4,book[0].begin());Bits bits(data.slice(d*4,data.size()-d*4),"codebook");
  for(unsigned i=1;i<=n;++i){book[i]=book[i-1];for(unsigned c=0;c<4;++c){const auto width=bits.take(3);for(unsigned lane=0;lane<d;++lane){const auto k=c+4*lane;if(width==7)book[i][k]=static_cast<std::uint8_t>(bits.take(8));else{const auto delta=bits.take(width);const bool negative=delta && bits.take(1);book[i][k]=static_cast<std::uint8_t>(int(book[i-1][k])+(negative?-int(delta):int(delta)));}}}}
  book_=std::move(book);
}
void CinemaDecoder::frame(Bytes h,Bytes data) {
  const auto offset=h.u32(4);const unsigned n=h.u16(8),d=h.u16(10),w=h.u32(12),rows=h.u32(16);
  if((d!=2 && d!=4) || !n || n>4095 || w!=info_.width || rows!=info_.height || w%d)throw ParseError("unsupported CNM frame layout");
  decoded_pixels_+=std::uint64_t(w)*rows;if(decoded_pixels_>512ULL*1024*1024)throw ParseError("CNM decoded pixel budget exceeded");
  // Remaining frame fields are not read by the verified progressive decoder.
  if(offset<data.size())codebook(data.slice(offset,data.size()-offset),d,n);
  if(book_.empty())throw ParseError("CNM frame has no codebook");
  const std::size_t across=w/d,total=across*rows;unsigned literal_bits=0;for(unsigned v=n;v;v>>=1)++literal_bits;
  Bits bits(data,"indices");
  std::vector<std::uint16_t> indices;indices.reserve(total+7);
  std::vector<std::uint32_t> seen(65536,0);
  struct Ring {std::array<std::uint16_t,16> values{};unsigned cursor=0;};
  std::map<std::uint16_t,Ring> mru;
  const auto above=[&](std::size_t k){if(k<across)throw ParseError("CNM up reference precedes first row");return indices.at(k-across);};
  while(indices.size()<total) {
    if(bits.take(1)) {
      for(unsigned i=0;i<8;++i){const auto k=indices.size();indices.push_back(above(k));}
      continue;
    }
    for(unsigned i=0;i<8;++i) {
      const auto k=indices.size();const std::size_t col=2*(k%across)+1,pos=2*k+1;
      const auto code=bits.peek(3);std::uint16_t value=0;bool update=false;
      if(code&1){bits.take(1);value=above(k);}
      else {
        bits.take(3);
        if(code==0){value=static_cast<std::uint16_t>(bits.take(literal_bits));update=true;}
        else if(code==4){const auto delta=bits.take(4)+1;const bool negative=bits.take(1);const auto up=above(k);value=static_cast<std::uint16_t>(negative?unsigned(up)-delta:unsigned(up)+delta);update=true;}
        else if(code==6){const auto slot=bits.take(4);value=mru[above(k)].values[slot];}
        else if(code==2){
          unsigned count=0;if(k>=across)seen[above(k)]=static_cast<std::uint32_t>(k);
          const auto add=[&](std::size_t index){if(index>=indices.size())throw ParseError("invalid CNM neighbor reference");const auto neighbor=indices[index];if(seen[neighbor]!=k){if(count>=4)throw ParseError("CNM neighbor list overflow");candidates_[count++]=neighbor;seen[neighbor]=static_cast<std::uint32_t>(k);}};
          if(k>0 && col>=2)add(k-1);
          if(pos>=2*across+2 && col>=2)add(k-across-1);
          if(pos>=2*across-2 && col<=2*across-2)add(k-across+1);
          if(pos>=4*across)add(k-2*across);
          const auto select=count==1?0:bits.take(count<=2?1:2);value=candidates_[select];update=true;
        }else throw ParseError("invalid CNM index token");
      }
      if(k<total && value>=book_.size())throw ParseError("CNM codebook index "+std::to_string(value)+" outside table "+std::to_string(book_.size())+" at vector "+std::to_string(k)+" of "+std::to_string(total));
      if(update && k>=across){auto& ring=mru[above(k)];ring.values[ring.cursor]=value;ring.cursor=(ring.cursor+1)%16;}
      indices.push_back(value);
    }
  }
  for(std::size_t k=0;k<total;++k)if(indices[k]){const auto& vector=book_.at(indices[k]);std::copy(vector.begin(),vector.begin()+d*4,surface_.begin()+k*d*4);}
}
CinemaEvent CinemaDecoder::next() {
  const Bytes b(bytes_);b.require(position_,1);const auto tag=b.u8(position_++);
  CinemaEvent event{};
  if(tag=='Z' || tag=='A' || tag=='B') {
    event.kind=CinemaEvent::Audio;event.channel=tag=='Z'?0:tag=='A'?1:2;
    if(event.channel>=info_.sounds.size())throw ParseError("CNM sound chunk has no configuration");
    const auto size=b.u32(position_);position_+=4;b.require(position_,size);if(size%(info_.sounds[event.channel].channels*2))throw ParseError("unaligned CNM PCM chunk");
    const auto payload=b.slice(position_,size);event.pcm.assign(payload.data(),payload.data()+size);position_+=size;
  }else if(tag=='T') {
    event.kind=CinemaEvent::Codebook;const auto size=b.u32(position_);const auto n=b.u16(position_+4),d=b.u16(position_+6);position_+=8;b.require(position_,size);codebook(b.slice(position_,size),d,n);position_+=size;
  }else if(tag=='S' || tag=='U' || tag=='X') {
    const auto h=b.slice(position_,47);position_+=47;const auto size=h.u32(0);const auto payload=b.slice(position_,size);position_+=size;
    if(++frame_count_>info_.frames)throw ParseError("CNM frame count exceeds header");
    event.kind=tag=='X'?CinemaEvent::Dropped:CinemaEvent::Frame;if(tag!='X')frame(h,payload);
  }else throw ParseError("unsupported CNM chunk tag: "+std::to_string(tag));
  if(done() && frame_count_!=info_.frames)throw ParseError("CNM EOF before declared frame count");
  return event;
}
Bitmap CinemaDecoder::image() const {
  Bitmap result{info_.width,info_.height,std::vector<std::uint8_t>(surface_.size())};
  for(unsigned y=0;y<info_.height;++y)for(unsigned x=0;x<info_.width;++x){const auto s=(std::size_t(info_.height-1-y)*info_.width+x)*4,d=(std::size_t(y)*info_.width+x)*4;result.rgba[d]=surface_[s+2];result.rgba[d+1]=surface_[s+1];result.rgba[d+2]=surface_[s];result.rgba[d+3]=255;}
  return result;
}
}
