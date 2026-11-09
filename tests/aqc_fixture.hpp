#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fixture {
using Data=std::vector<std::uint8_t>;
inline void integer(Data& out,std::uint32_t value) {
  for (unsigned i=0;i<4;++i) out.push_back(static_cast<std::uint8_t>(value>>(i*8)));
}
inline void real(Data& out,float value) {
  std::uint32_t word;
  std::memcpy(&word,&value,4); integer(out,word);
}
inline std::string bits(unsigned value,unsigned count) {
  std::string text;
  for (unsigned i=count;i>0;--i) text+=((value>>(i-1))&1U) ? '1' : '0';
  return text;
}
inline Data pack(const std::string& text) {
  Data result((text.size()+7)/8,0);
  for (std::size_t i=0;i<text.size();++i) if (text[i]=='1') result[i/8]|=std::uint8_t(1U<<(7-i%8));
  return result;
}
inline void append(Data& out,const Data& data) { out.insert(out.end(),data.begin(),data.end()); }
inline Data header(unsigned left,unsigned top,unsigned width,unsigned height) {
  Data h;
  integer(h,8); integer(h,4); integer(h,2);
  for (float angle:{0.f,360.f,-75.f,75.f}) real(h,angle);
  for (unsigned v:{left,left+width,top,top+height,width*height*2,width*2}) integer(h,v);
  return h;
}
inline Data frame(unsigned left,unsigned top,unsigned index) {
  const auto payload=pack("0"+bits(index,13)+"11");
  Data data; integer(data,static_cast<unsigned>(payload.size()));
  append(data,header(left,top,4,2)); append(data,payload); return data;
}
inline Data base() {
  Data data;
  const auto indices=pack("0"+bits(0,13)+std::string(14,'1'));
  integer(data,static_cast<unsigned>(indices.size())); append(data,header(0,0,8,4)); append(data,indices);
  std::string tokens;
  for (unsigned color:{0xf800U,0x07e0U,0x001fU,0xffffU}) tokens+="0"+bits(color,16);
  tokens+=std::string(32396*2,'1');
  const auto palette=pack(tokens);
  integer(data,static_cast<unsigned>(palette.size())); append(data,palette);
  return data;
}
inline Data animated() {
  auto data=base();
  integer(data,1); real(data,0); integer(data,0); append(data,frame(0,0,0));
  integer(data,2); real(data,12); integer(data,1);
  append(data,frame(0,1,1)); append(data,frame(4,2,0)); return data;
}
}
