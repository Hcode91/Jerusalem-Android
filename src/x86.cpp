// Integer-subset x86-32 interpreter for the original story handlers. See x86.hpp.
#include "jerusalem/x86.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace jerusalem {
namespace {
constexpr std::uint32_t stack_base=0x00110000, stack_size=0x20000;
constexpr std::uint32_t scratch_base=0x00200000, scratch_size=0x100000;
constexpr std::uint32_t return_magic=0xfffff000;
enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
std::string hex(std::uint32_t v){std::ostringstream o;o<<"0x"<<std::hex<<v;return o.str();}
}

X86Machine::X86Machine(Bytes b) {
  if (b.size()<0x400 || !b.is(0,"MZ")) throw ParseError("x86: not a PE file");
  const auto pe=b.u32(0x3c);
  if (!b.is(pe,std::string("PE\0\0",4)) || b.u16(pe+4)!=0x14c) throw ParseError("x86: not an i386 PE");
  const auto sections=b.u16(pe+6), optional=b.u16(pe+20);
  const auto image_base=b.u32(pe+24+28), image_size=b.u32(pe+24+56);
  if (image_base!=0x400000 || image_size>0x200000) throw ParseError("x86: unexpected image layout");
  Region image{image_base,std::vector<std::uint8_t>(image_size,0),true};
  std::copy_n(b.data(),std::min<std::size_t>(0x1000,b.size()),image.bytes.begin());
  for (unsigned i=0;i<sections;++i) {
    const auto s=pe+24+optional+40*i;
    const auto va=b.u32(s+12), raw_size=b.u32(s+16), raw=b.u32(s+20);
    if (va>=image_size) throw ParseError("x86: section outside image");
    const auto n=std::min<std::uint64_t>({raw_size,image_size-va,b.size()>raw?b.size()-raw:0});
    std::copy_n(b.data()+raw,n,image.bytes.begin()+va);
  }
  regions_.push_back(std::move(image));
  regions_.push_back({stack_base,std::vector<std::uint8_t>(stack_size,0),true});
  regions_.push_back({scratch_base,std::vector<std::uint8_t>(scratch_size,0),true});
}

void X86Machine::trap(std::uint32_t address,Native native,unsigned args,bool callee_pops) {
  if (args>16) throw ParseError("x86: too many trapped arguments");
  traps_[address]={std::move(native),args,callee_pops};
}

void X86Machine::restore_image(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size()!=regions_.front().bytes.size()) throw ParseError("x86: image size mismatch");
  regions_.front().bytes=bytes;
}

std::uint32_t X86Machine::scratch(std::size_t bytes) {
  bytes=(bytes+15)&~std::size_t(15);
  if (bytes>scratch_size-scratch_used_) throw ParseError("x86: scratch memory exhausted");
  const auto address=scratch_base+static_cast<std::uint32_t>(scratch_used_);
  std::fill_n(regions_[2].bytes.begin()+scratch_used_,bytes,0);
  scratch_used_+=bytes;
  return address;
}
void X86Machine::reset_scratch() { scratch_used_=0; }

const std::uint8_t* X86Machine::at(std::uint32_t address,std::size_t size) const {
  for (const auto& r:regions_)
    if (address>=r.base && std::uint64_t(address)-r.base+size<=r.bytes.size()) return r.bytes.data()+(address-r.base);
  throw ParseError("x86: access outside the sandbox at "+hex(address)+" (eip "+hex(eip_)+", last call at "+hex(last_call_)+")");
}
std::uint8_t* X86Machine::at(std::uint32_t address,std::size_t size,bool write) {
  for (auto& r:regions_)
    if (address>=r.base && std::uint64_t(address)-r.base+size<=r.bytes.size()) {
      if (write && !r.writable) throw ParseError("x86: write to read-only memory");
      return r.bytes.data()+(address-r.base);
    }
  throw ParseError("x86: access outside the sandbox at "+hex(address)+" (eip "+hex(eip_)+", last call at "+hex(last_call_)+")");
}
std::uint8_t X86Machine::read8(std::uint32_t a) const { return *at(a,1); }
std::uint32_t X86Machine::read32(std::uint32_t a) const { std::uint32_t v; std::memcpy(&v,at(a,4),4); return v; }
void X86Machine::write8(std::uint32_t a,std::uint8_t v) { *at(a,1,true)=v; }
void X86Machine::write32(std::uint32_t a,std::uint32_t v) { std::memcpy(at(a,4,true),&v,4); }
std::string X86Machine::read_string(std::uint32_t a,std::size_t limit) const {
  std::string s;
  for (std::size_t i=0;i<limit;++i) { const auto c=static_cast<char>(read8(a+static_cast<std::uint32_t>(i))); if (!c) return s; s.push_back(c); }
  throw ParseError("x86: unterminated string");
}

std::uint32_t X86Machine::call(std::uint32_t function,std::uint32_t this_pointer,const std::vector<std::uint32_t>& args,std::uint64_t step_limit) {
  std::fill(std::begin(reg_),std::end(reg_),0);
  reg_[ESP]=stack_base+stack_size-64;
  for (auto i=args.rbegin();i!=args.rend();++i) { reg_[ESP]-=4; write32(reg_[ESP],*i); }
  reg_[ESP]-=4; write32(reg_[ESP],return_magic);
  reg_[ECX]=this_pointer; eip_=function;
  run(return_magic,step_limit);
  return reg_[EAX];
}

void X86Machine::run(std::uint32_t stop,std::uint64_t step_limit) {
  std::uint64_t n=0;
  while (eip_!=stop) {
    if (++n>step_limit) throw ParseError("x86: step limit exceeded at "+hex(eip_));
    if (const auto t=traps_.find(eip_); t!=traps_.end()) {
      Call c{eip_,reg_[ECX],{}};
      const auto ret=read32(reg_[ESP]);
      for (unsigned i=0;i<t->second.args;++i) c.args.push_back(read32(reg_[ESP]+4+4*i));
      reg_[EAX]=t->second.native(c);
      reg_[ESP]+=4+(t->second.callee_pops?4*t->second.args:0);
      // Trapped callees follow the MSVC convention: ECX/EDX are clobbered, others preserved.
      reg_[ECX]=0; reg_[EDX]=0;
      eip_=ret;
      continue;
    }
    step();
  }
  steps_+=n;
}

// ---------------------------------------------------------------- decoder

struct X86Decoder {
  X86Machine& m;
  bool opsize16=false;
  std::uint8_t fetch8() { return m.read8(m.eip_++); }
  std::uint32_t fetch32() { const auto v=m.read32(m.eip_); m.eip_+=4; return v; }
  std::uint16_t fetch16() { const auto v=std::uint16_t(m.read8(m.eip_)|m.read8(m.eip_+1)<<8); m.eip_+=2; return v; }
  std::uint32_t fetchimm(unsigned size) { return size==1?fetch8():size==2?fetch16():fetch32(); }
  static std::uint32_t sext8(std::uint8_t v) { return std::uint32_t(std::int32_t(std::int8_t(v))); }

  struct Operand { bool reg; unsigned index; std::uint32_t address; };
  unsigned modrm_reg=0;
  Operand modrm() {
    const auto b=fetch8();
    const unsigned mod=b>>6,reg=(b>>3)&7,rm=b&7;
    modrm_reg=reg;
    if (mod==3) return {true,rm,0};
    std::uint32_t address=0;
    if (rm==4) {
      const auto sib=fetch8();
      const unsigned scale=sib>>6,index=(sib>>3)&7,base=sib&7;
      if (base==5 && mod==0) address=fetch32(); else address=m.reg_[base];
      if (index!=4) address+=m.reg_[index]<<scale;
    } else if (rm==5 && mod==0) address=fetch32();
    else address=m.reg_[rm];
    if (mod==1) address+=sext8(fetch8()); else if (mod==2) address+=fetch32();
    return {false,0,address};
  }
  std::uint32_t get(const Operand& o,unsigned size) {
    if (o.reg) {
      if (size==4) return m.reg_[o.index];
      if (size==2) return m.reg_[o.index]&0xffff;
      return o.index<4?m.reg_[o.index]&0xff:(m.reg_[o.index-4]>>8)&0xff;
    }
    if (size==4) return m.read32(o.address);
    if (size==2) return m.read8(o.address)|m.read8(o.address+1)<<8;
    return m.read8(o.address);
  }
  void set(const Operand& o,unsigned size,std::uint32_t v) {
    if (o.reg) {
      auto& r=m.reg_[size==1 && o.index>=4?o.index-4:o.index];
      if (size==4) r=v;
      else if (size==2) r=(r&0xffff0000)|(v&0xffff);
      else if (o.index<4) r=(r&0xffffff00)|(v&0xff);
      else r=(r&0xffff00ff)|((v&0xff)<<8);
      return;
    }
    if (size==4) m.write32(o.address,v);
    else if (size==2) { m.write8(o.address,std::uint8_t(v)); m.write8(o.address+1,std::uint8_t(v>>8)); }
    else m.write8(o.address,std::uint8_t(v));
  }
  Operand reg_operand(unsigned index) { return {true,index,0}; }

  static std::uint32_t mask(unsigned size) { return size==4?0xffffffffU:size==2?0xffffU:0xffU; }
  static std::uint32_t sign(unsigned size) { return size==4?0x80000000U:size==2?0x8000U:0x80U; }
  void logic_flags(std::uint32_t r,unsigned size) { r&=mask(size); m.zf_=r==0; m.sf_=(r&sign(size))!=0; m.cf_=false; m.of_=false; }
  // ALU group: 0 add,1 or,2 adc,3 sbb,4 and,5 sub,6 xor,7 cmp
  std::uint32_t alu(unsigned op,std::uint32_t a,std::uint32_t b,unsigned size) {
    const auto mk=mask(size),sg=sign(size);
    a&=mk; b&=mk;
    std::uint64_t r=0;
    switch (op) {
      case 0: case 2: {
        const std::uint64_t carry=op==2 && m.cf_?1:0;
        r=std::uint64_t(a)+b+carry;
        m.cf_=r>mk; const auto res=std::uint32_t(r)&mk;
        m.of_=((a^res)&(b^res)&sg)!=0; m.zf_=res==0; m.sf_=(res&sg)!=0; return res;
      }
      case 3: case 5: case 7: {
        const std::uint64_t borrow=op==3 && m.cf_?1:0;
        m.cf_=std::uint64_t(a)<std::uint64_t(b)+borrow;
        const auto res=std::uint32_t(std::uint64_t(a)-b-borrow)&mk;
        m.of_=((a^b)&(a^res)&sg)!=0; m.zf_=res==0; m.sf_=(res&sg)!=0; return res;
      }
      case 1: r=a|b; break;
      case 4: r=a&b; break;
      case 6: r=a^b; break;
    }
    logic_flags(std::uint32_t(r),size);
    return std::uint32_t(r)&mk;
  }
  bool condition(unsigned cc) {
    bool r;
    switch (cc>>1) {
      case 0: r=m.of_; break;
      case 1: r=m.cf_; break;
      case 2: r=m.zf_; break;
      case 3: r=m.cf_||m.zf_; break;
      case 4: r=m.sf_; break;
      case 5: throw ParseError("x86: parity condition unsupported");
      case 6: r=m.sf_!=m.of_; break;
      default: r=m.zf_||(m.sf_!=m.of_); break;
    }
    return (cc&1)?!r:r;
  }
  void push(std::uint32_t v) { m.reg_[ESP]-=4; m.write32(m.reg_[ESP],v); }
  std::uint32_t pop() { const auto v=m.read32(m.reg_[ESP]); m.reg_[ESP]+=4; return v; }
  std::uint32_t shift(unsigned op,std::uint32_t v,unsigned count,unsigned size) {
    count&=31; if (!count) return v&mask(size);
    const auto mk=mask(size),bits=size*8; v&=mk; std::uint32_t r=v;
    switch (op) {
      case 0: r=((v<<(count%bits))|(v>>((bits-count%bits)%bits)))&mk; m.cf_=r&1; break;  // rol
      case 1: r=((v>>(count%bits))|(v<<((bits-count%bits)%bits)))&mk; m.cf_=(r&sign(size))!=0; break;  // ror
      case 4: case 6: m.cf_=count<=bits && ((std::uint64_t(v)<<count)>>bits)&1; r=std::uint32_t(std::uint64_t(v)<<count)&mk; m.zf_=r==0; m.sf_=(r&sign(size))!=0; break;
      case 5: m.cf_=count<=bits && (v>>(count-1))&1; r=count>=bits?0:v>>count; m.zf_=r==0; m.sf_=(r&sign(size))!=0; break;
      case 7: {
        std::int64_t sv=(v&sign(size))?std::int64_t(v)-(std::int64_t(1)<<bits):std::int64_t(v);
        m.cf_=(sv>>(std::min(count,bits)-1))&1; r=std::uint32_t(sv>>std::min(count,bits-1))&mk; m.zf_=r==0; m.sf_=(r&sign(size))!=0; break;
      }
      default: throw ParseError("x86: unsupported shift");
    }
    if (count==1 && (op==4 || op==6)) m.of_=((r&sign(size))!=0)!=m.cf_;
    return r;
  }

  void step() {
    const auto start=m.eip_;
    auto op=fetch8();
    bool rep=false;
    for (;;) {
      if (op==0x66) { opsize16=true; op=fetch8(); continue; }
      if (op==0xf3) { rep=true; op=fetch8(); continue; }
      break;
    }
    const unsigned osz=opsize16?2:4;
    // ALU r/m forms
    if (op<0x40 && (op&7)<6) {
      const unsigned group=op>>3, form=op&7;
      if (form<4) {
        const unsigned size=(form&1)?osz:1;
        auto rm=modrm(); auto r=reg_operand(modrm_reg);
        const bool to_reg=form&2;
        const auto a=to_reg?get(r,size):get(rm,size), b=to_reg?get(rm,size):get(r,size);
        const auto res=alu(group,a,b,size);
        if (group!=7) set(to_reg?r:rm,size,res);
      } else {
        const unsigned size=form==4?1:osz;
        const auto imm=fetchimm(size);
        const auto res=alu(group,get(reg_operand(EAX),size),imm,size);
        if (group!=7) set(reg_operand(EAX),size,res);
      }
      return;
    }
    if (op>=0x40 && op<0x50) {  // inc/dec r32 (CF preserved)
      const bool cf=m.cf_; auto o=reg_operand(op&7);
      set(o,osz,alu(op<0x48?0:5,get(o,osz),1,osz)); m.cf_=cf; return;
    }
    if (op>=0x50 && op<0x58) { push(m.reg_[op&7]); return; }
    if (op>=0x58 && op<0x60) { m.reg_[op&7]=pop(); return; }
    if (op==0x68) { push(fetch32()); return; }
    if (op==0x6a) { push(sext8(fetch8())); return; }
    if (op==0x69 || op==0x6b) {
      auto rm=modrm(); const auto reg=modrm_reg;
      const auto imm=op==0x6b?sext8(fetch8()):fetch32();
      const std::int64_t r=std::int64_t(std::int32_t(get(rm,4)))*std::int32_t(imm);
      m.reg_[reg]=std::uint32_t(r); m.cf_=m.of_=r!=std::int64_t(std::int32_t(r)); return;
    }
    if (op>=0x70 && op<0x80) { const auto d=sext8(fetch8()); if (condition(op&15)) m.eip_+=d; return; }
    if (op>=0x80 && op<=0x83) {
      const unsigned size=op==0x80?1:osz;
      auto rm=modrm(); const auto group=modrm_reg;
      const auto imm=op==0x81?fetchimm(size):op==0x83?sext8(fetch8()):fetch8();
      const auto res=alu(group,get(rm,size),imm,size);
      if (group!=7) set(rm,size,res);
      return;
    }
    if (op==0x84 || op==0x85) { const unsigned size=op==0x84?1:osz; auto rm=modrm(); logic_flags(get(rm,size)&get(reg_operand(modrm_reg),size),size); return; }
    if (op==0x86 || op==0x87) { const unsigned size=op==0x86?1:osz; auto rm=modrm(); auto r=reg_operand(modrm_reg); const auto a=get(rm,size),b=get(r,size); set(rm,size,b); set(r,size,a); return; }
    if (op>=0x88 && op<=0x8b) {
      const unsigned size=(op&1)?osz:1; auto rm=modrm(); auto r=reg_operand(modrm_reg);
      if (op&2) set(r,size,get(rm,size)); else set(rm,size,get(r,size));
      return;
    }
    if (op==0x8d) { auto rm=modrm(); if (rm.reg) throw ParseError("x86: lea of register"); m.reg_[modrm_reg]=rm.address; return; }
    if (op==0x8f) { auto rm=modrm(); set(rm,4,pop()); return; }
    if (op==0x90) return;
    if (op==0x98) { m.reg_[EAX]=opsize16?(std::uint32_t(std::int8_t(m.reg_[EAX]))&0xffff)|(m.reg_[EAX]&0xffff0000):std::uint32_t(std::int32_t(std::int16_t(m.reg_[EAX]))); return; }
    if (op==0x99) { m.reg_[EDX]=(m.reg_[EAX]&0x80000000U)?0xffffffffU:0; return; }
    if (op==0xa0 || op==0xa1) { const auto a=fetch32(); set(reg_operand(EAX),op==0xa0?1:osz,op==0xa0?m.read8(a):get({false,0,a},osz)); return; }
    if (op==0xa2 || op==0xa3) { const auto a=fetch32(); set({false,0,a},op==0xa2?1:osz,get(reg_operand(EAX),op==0xa2?1:osz)); return; }
    if (op==0xa8 || op==0xa9) { const unsigned size=op==0xa8?1:osz; logic_flags(get(reg_operand(EAX),size)&fetchimm(size),size); return; }
    if (op==0xab) {  // stos
      std::uint32_t count=rep?m.reg_[ECX]:1;
      if (count>0x100000) throw ParseError("x86: oversized string store");
      for (;count;--count) { set({false,0,m.reg_[EDI]},osz,m.reg_[EAX]); m.reg_[EDI]+=osz; }
      if (rep) m.reg_[ECX]=0;
      return;
    }
    if (op>=0xb0 && op<0xb8) { set(reg_operand(op&7),1,fetch8()); return; }
    if (op>=0xb8 && op<0xc0) { set(reg_operand(op&7),osz,fetchimm(osz)); return; }
    if (op==0xc0 || op==0xc1 || (op>=0xd0 && op<=0xd3)) {
      const unsigned size=(op&1)?osz:1; auto rm=modrm(); const auto group=modrm_reg;
      const unsigned count=op<=0xc1?fetch8():op<=0xd1?1:m.reg_[ECX]&0xff;
      set(rm,size,shift(group,get(rm,size),count,size)); return;
    }
    if (op==0xc2) { const auto n=fetch16(); m.eip_=pop(); m.reg_[ESP]+=n; return; }
    if (op==0xc3) { m.eip_=pop(); return; }
    if (op==0xc6 || op==0xc7) { const unsigned size=op==0xc6?1:osz; auto rm=modrm(); if (modrm_reg) throw ParseError("x86: bad mov"); set(rm,size,fetchimm(size)); return; }
    if (op==0xe8) { m.last_call_=start; const auto d=fetch32(); push(m.eip_); m.eip_+=d; return; }
    if (op==0xe9) { const auto d=fetch32(); m.eip_+=d; return; }
    if (op==0xeb) { const auto d=sext8(fetch8()); m.eip_+=d; return; }
    if (op==0xf6 || op==0xf7) {
      const unsigned size=op==0xf6?1:osz; auto rm=modrm(); const auto group=modrm_reg;
      const auto v=get(rm,size);
      switch (group) {
        case 0: logic_flags(v&fetchimm(size),size); return;
        case 2: set(rm,size,~v); return;
        case 3: { const auto r=alu(5,0,v,size); set(rm,size,r); m.cf_=v!=0; return; }
        case 4: case 5: {
          if (size!=4) throw ParseError("x86: narrow multiply unsupported");
          const std::int64_t r=group==4?std::int64_t(std::uint64_t(m.reg_[EAX])*v):std::int64_t(std::int32_t(m.reg_[EAX]))*std::int32_t(v);
          m.reg_[EAX]=std::uint32_t(r); m.reg_[EDX]=std::uint32_t(std::uint64_t(r)>>32);
          m.cf_=m.of_=group==4?m.reg_[EDX]!=0:r!=std::int64_t(std::int32_t(r)); return;
        }
        case 6: case 7: {
          if (size!=4 || !v) throw ParseError("x86: unsupported division");
          if (group==6) { const std::uint64_t n=(std::uint64_t(m.reg_[EDX])<<32)|m.reg_[EAX]; const auto q=n/v; if (q>0xffffffffULL) throw ParseError("x86: division overflow"); m.reg_[EAX]=std::uint32_t(q); m.reg_[EDX]=std::uint32_t(n%v); }
          else { const std::int64_t n=std::int64_t((std::uint64_t(m.reg_[EDX])<<32)|m.reg_[EAX]); const auto d=std::int64_t(std::int32_t(v)); const auto q=n/d; if (q!=std::int64_t(std::int32_t(q))) throw ParseError("x86: division overflow"); m.reg_[EAX]=std::uint32_t(q); m.reg_[EDX]=std::uint32_t(n%d); }
          return;
        }
        default: throw ParseError("x86: unsupported group-3 operation");
      }
    }
    if (op==0xfe || op==0xff) {
      const unsigned size=op==0xfe?1:osz; auto rm=modrm(); const auto group=modrm_reg;
      switch (group) {
        case 0: case 1: { const bool cf=m.cf_; set(rm,size,alu(group?5:0,get(rm,size),1,size)); m.cf_=cf; return; }
        case 2: if (op==0xfe) break; { m.last_call_=start; const auto t=get(rm,4); push(m.eip_); m.eip_=t; return; }
        case 4: if (op==0xfe) break; m.eip_=get(rm,4); return;
        case 6: if (op==0xfe) break; push(get(rm,4)); return;
      }
      throw ParseError("x86: unsupported group-5 operation");
    }
    if (op==0x0f) {
      const auto op2=fetch8();
      if (op2>=0x80 && op2<0x90) { const auto d=fetch32(); if (condition(op2&15)) m.eip_+=d; return; }
      if (op2>=0x90 && op2<0xa0) { auto rm=modrm(); set(rm,1,condition(op2&15)?1:0); return; }
      if (op2==0xaf) { auto rm=modrm(); const std::int64_t r=std::int64_t(std::int32_t(m.reg_[modrm_reg]))*std::int32_t(get(rm,4)); m.reg_[modrm_reg]=std::uint32_t(r); m.cf_=m.of_=r!=std::int64_t(std::int32_t(r)); return; }
      if (op2==0xb6 || op2==0xb7 || op2==0xbe || op2==0xbf) {
        const unsigned size=(op2&1)?2:1; auto rm=modrm(); auto v=get(rm,size);
        if (op2>=0xbe) v=size==1?sext8(std::uint8_t(v)):std::uint32_t(std::int32_t(std::int16_t(v)));
        set(reg_operand(modrm_reg),osz,v); return;
      }
    }
    std::ostringstream e; e<<"x86: unsupported instruction 0x"<<std::hex<<unsigned(op)<<" at 0x"<<start;
    throw ParseError(e.str());
  }
};

void X86Machine::step() { X86Decoder d{*this}; d.step(); }
}  // namespace jerusalem
