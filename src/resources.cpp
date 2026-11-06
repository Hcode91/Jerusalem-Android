#include "jerusalem/resources.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace jerusalem {
void Bytes::require(std::size_t p, std::size_t n) const {
  if (p > size_ || n > size_ - p) throw ParseError("truncated data at byte " + std::to_string(p));
}
std::uint8_t Bytes::u8(std::size_t p) const { require(p, 1); return data_[p]; }
std::uint16_t Bytes::u16(std::size_t p) const { require(p, 2); return data_[p] | (std::uint16_t(data_[p+1]) << 8); }
std::uint32_t Bytes::u32(std::size_t p) const {
  require(p, 4);
  return std::uint32_t(data_[p]) | (std::uint32_t(data_[p+1]) << 8) |
         (std::uint32_t(data_[p+2]) << 16) | (std::uint32_t(data_[p+3]) << 24);
}
std::int32_t Bytes::i32(std::size_t p) const {
  const auto n = u32(p);
  return n <= 0x7fffffffU ? static_cast<std::int32_t>(n) :
      static_cast<std::int32_t>(static_cast<std::int64_t>(n) - 0x100000000LL);
}
bool Bytes::is(std::size_t p, const std::string& s) const {
  return p <= size_ && s.size() <= size_ - p && (s.empty() || std::equal(s.begin(), s.end(), data_ + p));
}
Bytes Bytes::slice(std::size_t p, std::size_t n) const { require(p,n); return Bytes(p ? data_ + p : data_,n); }
std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) throw ParseError("cannot open file");
  const auto end = in.tellg();
  if (end < 0 || static_cast<std::uint64_t>(end) > max_resource_bytes)
    throw ParseError("resource exceeds 64 MiB inspection limit");
  std::vector<std::uint8_t> out(static_cast<std::size_t>(end));
  in.seekg(0);
  if (!out.empty() && !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size())))
    throw ParseError("short read");
  return out;
}
Wave parse_wave(Bytes b) {
  b.require(0,12);
  if (!b.is(0,"RIFF") || !b.is(8,"WAVE")) throw ParseError("not RIFF/WAVE");
  const std::uint64_t total = std::uint64_t(b.u32(4)) + 8;
  if (total < 12 || total > b.size()) throw ParseError("truncated or invalid RIFF length");
  Wave w{};
  w.file_size = static_cast<std::size_t>(total);
  bool fmt = false, data = false;
  std::uint16_t align = 0;
  for (std::size_t p = 12; p < total;) {
    if (total-p < 8) throw ParseError("truncated RIFF chunk header");
    auto n = b.u32(p+4);
    const std::uint64_t padded = std::uint64_t(n) + (n & 1U);
    if (padded > total-p-8) throw ParseError("truncated RIFF chunk or padding");
    if (b.is(p,"fmt ")) {
      if (fmt || n < 16) throw ParseError("duplicate or short WAVE fmt");
      if (b.u16(p+8) != 1) throw ParseError("unsupported WAVE codec: only integer PCM");
      w.channels = b.u16(p+10); w.rate = b.u32(p+12);
      align = b.u16(p+20); w.bits = b.u16(p+22);
      if ((w.channels != 1 && w.channels != 2) || (w.bits != 8 && w.bits != 16) ||
          w.rate < 1000 || w.rate > 192000 || align != w.channels*(w.bits/8) ||
          b.u32(p+16) != w.rate*align) throw ParseError("invalid or unsupported PCM geometry");
      fmt = true;
    } else if (b.is(p,"data")) {
      if (data) throw ParseError("duplicate WAVE data");
      w.data_offset = p+8; w.data_size = n; data = true;
    }
    p += 8 + static_cast<std::size_t>(padded);
  }
  if (!fmt || !data || w.data_size == 0 || w.data_size % align != 0)
    throw ParseError("missing or unaligned PCM samples");
  return w;
}
Bitmap decode_bitmap(Bytes b) {
  b.require(0,54);
  if (!b.is(0,"BM")) throw ParseError("not BMP");
  const auto total = b.u32(2), offset = b.u32(10), dib = b.u32(14);
  if (total < 54 || total > b.size() || dib < 40 || dib > total-14 || offset < 14+dib || offset > total)
    throw ParseError("invalid or truncated BMP headers");
  auto width = b.i32(18), signed_height = b.i32(22);
  const auto bits = b.u16(28);
  if (width <= 0 || width > 8192 || signed_height == 0 || signed_height < -8192 || signed_height > 8192 ||
      b.u16(26) != 1 || (bits != 24 && bits != 32) || b.u32(30) != 0)
    throw ParseError("unsupported BMP: require BI_RGB 24/32-bit, dimensions 1..8192");
  const auto height = static_cast<std::uint32_t>(signed_height < 0 ? -signed_height : signed_height);
  const std::size_t stride = ((std::size_t(width)*bits+31)/32)*4;
  if (height > (total-offset)/stride || std::uint64_t(width)*height*4 > max_resource_bytes)
    throw ParseError("truncated or oversized BMP pixels");
  Bitmap out{static_cast<std::uint32_t>(width), height, {}};
  out.rgba.resize(std::size_t(width)*height*4);
  for (std::uint32_t y=0; y<height; ++y) {
    const auto src_y = signed_height < 0 ? y : height-1-y;
    for (std::int32_t x=0; x<width; ++x) {
      const auto src = offset+src_y*stride+std::size_t(x)*(bits/8);
      const auto dst = (std::size_t(y)*width+x)*4;
      out.rgba[dst] = b.u8(src+2); out.rgba[dst+1] = b.u8(src+1); out.rgba[dst+2] = b.u8(src);
      // BI_RGB's fourth byte is reserved, not a verified alpha channel.
      out.rgba[dst+3] = 255;
    }
  }
  return out;
}
static std::string trim(std::string s) {
  const auto first = s.find_first_not_of(" \t\r");
  if (first == std::string::npos) return {};
  return s.substr(first, s.find_last_not_of(" \t\r")-first+1);
}
Cue parse_cue(const std::string& text) {
  if (text.size() > 65536 || text.find('\0') != std::string::npos) throw ParseError("invalid CUE text");
  std::istringstream input(text);
  std::string line, file;
  int phase=0;
  while (std::getline(input,line)) {
    line = trim(line);
    if (line.empty() || line.rfind("REM ",0)==0) continue;
    if (phase==0 && line.rfind("FILE \"",0)==0) {
      const auto end = line.find('"',6);
      if (end==std::string::npos || trim(line.substr(end+1))!="BINARY") throw ParseError("unsupported CUE FILE");
      file=line.substr(6,end-6);
      if (file.empty() || file=="." || file==".." || file.find_first_of("/\\:")!=std::string::npos)
        throw ParseError("CUE binary must be a sibling basename");
      phase=1;
    } else if (phase==1 && line=="TRACK 01 MODE1/2352") phase=2;
    else if (phase==2 && line=="INDEX 01 00:00:00") phase=3;
    else throw ParseError("unsupported CUE layout: only one MODE1/2352 track at zero");
  }
  if (phase!=3) throw ParseError("incomplete CUE layout");
  return {file,2352};
}
std::vector<IniEntry> parse_ini(const std::string& text) {
  if (text.size()>4*1024*1024 || text.find('\0')!=std::string::npos) throw ParseError("invalid INI text");
  std::vector<IniEntry> out;
  std::istringstream input(text); std::string line, section; std::size_t number=0;
  while (std::getline(input,line)) {
    ++number; line=trim(line);
    if (line.empty() || line[0]==';' || line[0]=='#') continue;
    if (line[0]=='[' && line.back()==']') { section=line.substr(1,line.size()-2); continue; }
    const auto eq=line.find('=');
    if (eq==std::string::npos || trim(line.substr(0,eq)).empty()) throw ParseError("unsupported INI syntax at line "+std::to_string(number));
    out.push_back({section,trim(line.substr(0,eq)),trim(line.substr(eq+1)),number});
  }
  return out;
}
ArtDirectory parse_at3(Bytes b, bool allow_prefix) {
  b.require(0,32);
  if (!b.is(0,std::string("ATIII\0\0\0",8))) throw ParseError("unsupported AT3 signature");
  const auto table_size=b.u32(8), count=b.u32(12), record_size=b.u32(16);
  if (record_size!=255 || count>100000 || std::uint64_t(count)*255!=table_size)
    throw ParseError("invalid or unsupported AT3 directory geometry");
  const std::size_t available=std::min<std::size_t>(count,(b.size()-32)/255);
  ArtDirectory out{count, available==count, {}};
  if (!out.directory_complete && !allow_prefix) throw ParseError("truncated AT3 directory");
  for (std::size_t i=0;i<available;++i) {
    const auto p=32+i*255;
    std::string name;
    for (std::size_t k=0;k<243 && b.u8(p+k);++k) name+=static_cast<char>(b.u8(p+k));
    if (name.empty() || name.size()==243) throw ParseError("invalid AT3 entry name");
    ArtEntry e{name,b.u32(p+243),b.u32(p+247),b.u32(p+251),false};
    if (std::uint64_t(e.offset)<std::uint64_t(32)+table_size || e.stored_size==0)
      throw ParseError("invalid AT3 payload offset or size");
    e.payload_available=e.offset<=b.size() && e.stored_size<=b.size()-e.offset;
    if (!e.payload_available && !allow_prefix) throw ParseError("truncated AT3 payload");
    out.entries.push_back(e);
  }
  return out;
}
std::vector<LocalizedEntry> parse_localized(const std::string& text) {
  if (text.size()>4*1024*1024 || text.find('\0')!=std::string::npos) throw ParseError("invalid localization text");
  std::istringstream input(text); std::string line, key;
  std::vector<LocalizedEntry> out;
  bool has_translation=false;
  while (std::getline(input,line)) {
    line=trim(line);
    if (line.empty()) continue;
    const auto marker=line.find('#');
    if (marker==std::string::npos) {
      if (!key.empty() && !has_translation) throw ParseError("missing localization translations");
      if (line.find_first_of(" \t")!=std::string::npos) throw ParseError("invalid localization key");
      key=line; has_translation=false; continue;
    }
    const auto language=trim(line.substr(0,marker));
    if (key.empty() || language.size()!=3 || !std::all_of(language.begin(),language.end(),[](char c){return c>='A'&&c<='Z';}))
      throw ParseError("invalid localization language or missing key");
    const auto second=line.find('#',marker+1);
    if (second==std::string::npos) throw ParseError("missing localization field delimiter");
    // Field meanings differ: aObj uses #label#, aMes uses #title#body.
    out.push_back({key,language,line.substr(marker+1,second-marker-1),line.substr(second+1)});
    has_translation=true;
  }
  if (key.empty() || !has_translation) throw ParseError("empty or incomplete localization table");
  return out;
}
static std::uint32_t decimal(const std::string& token) {
  if (token.empty() || token.size()>10) throw ParseError("invalid decimal integer");
  std::uint64_t value=0;
  for (char c:token) {
    if (c<'0'||c>'9') throw ParseError("invalid decimal integer");
    value=value*10+static_cast<unsigned>(c-'0');
  }
  if (value>0x7fffffffU) throw ParseError("integer exceeds supported signed 32-bit range");
  return static_cast<std::uint32_t>(value);
}
Dan parse_dan(const std::string& text) {
  if (text.size()>4*1024*1024 || text.find('\0')!=std::string::npos) throw ParseError("invalid DAN text");
  std::istringstream input(text); std::string token;
  auto next=[&]() { if (!(input>>token)) throw ParseError("truncated DAN"); return token; };
  const auto count=decimal(next());
  if (count>10000) throw ParseError("too many DAN levels");
  Dan out{};
  auto triple=[&]() { auto a=decimal(next()); auto b=decimal(next()); auto c=decimal(next()); return DanTriple{a,b,c}; };
  for (std::uint32_t i=0;i<count;++i) out.levels.push_back(triple());
  for (;;) {
    auto first=decimal(next()); const auto second=next();
    if (second=="END") { out.end=first; break; }
    auto b=decimal(second); auto c=decimal(next());
    if (first>b || (!out.intervals.empty() && first<=out.intervals.back().second))
      throw ParseError("invalid or overlapping DAN interval");
    out.intervals.push_back({first,b,c});
    if (out.intervals.size()>100000) throw ParseError("too many DAN intervals");
  }
  if ((!out.intervals.empty() && out.end<=out.intervals.back().second) || (input>>token))
    throw ParseError("invalid DAN terminator or trailing tokens");
  return out;
}
std::vector<Embedded> scan_media(Bytes b) {
  // Signature candidates only. Full structural validation is mandatory before extraction.
  std::vector<Embedded> out;
  for (std::size_t p=0; p<b.size();) {
    std::size_t length=0; std::string kind;
    try {
      if (b.is(p,"RIFF") && b.is(p+8,"WAVE")) {
        const auto w=parse_wave(b.slice(p,b.size()-p)); length=w.file_size; kind="wav";
      } else if (b.is(p,"BM")) {
        b.require(p,14);
        const auto n=b.u32(p+2);
        if (n<=b.size()-p) { decode_bitmap(b.slice(p,n)); length=n; kind="bmp"; }
      }
    } catch (const ParseError&) { /* malformed candidate is not an extracted asset */ }
    if (length) { out.push_back({kind,p,length}); p+=length; } else ++p;
  }
  return out;
}
std::string inspect(Bytes b) {
  std::ostringstream out; out << b.size() << " bytes. ";
  if (b.is(0,"RIFF") && b.is(8,"WAVE")) {
    const auto w=parse_wave(b);
    out << "PCM WAVE: " << w.channels << " channel(s), " << w.rate << " Hz, " << w.bits << " bit, " << w.data_size << " audio bytes.";
  } else if (b.is(0,"BM")) {
    const auto image=decode_bitmap(b); out << "BMP: " << image.width << " x " << image.height << ".";
  } else if (b.is(0,"ATIII")) {
    const auto art=parse_at3(b,true);
    out << "AT3 directory: " << art.entries.size() << "/" << art.declared_count << " records available. ";
    if (!art.directory_complete) out << "TRUNCATED directory prefix. ";
    out << "Payload compression is not decoded.";
  } else {
    out << "No supported media header at offset zero. Proprietary formats are not decoded. Header:";
    const char* hex="0123456789ABCDEF";
    for (std::size_t i=0; i<std::min<std::size_t>(32,b.size()); ++i)
      out << ' ' << hex[b.u8(i)>>4] << hex[b.u8(i)&15];
  }
  return out.str();
}

ResourceDescription describe_resource(Bytes b, const std::string& filename) {
  constexpr std::size_t display_limit=128*1024;
  auto result=[&](ResourceKind kind,std::string summary,std::string details) {
    if (details.size()>display_limit) { details.resize(display_limit); details+="\n[Display truncated]"; }
    return ResourceDescription{kind,std::move(summary),std::move(details)};
  };
  if (b.is(0,"ATIII")) {
    const auto directory=parse_at3(b,true);
    const std::string summary="AT3 archive: "+std::to_string(directory.entries.size())+" / "+
        std::to_string(directory.declared_count)+" resource names available.";
    std::string details=summary+"\n";
    if (!directory.directory_complete) details+="This file is a truncated directory prefix.\n";
    details+="Stored images use a proprietary codec. They cannot be displayed yet.\n\nResources:\n";
    for (const auto& entry:directory.entries) {
      details+=entry.name;
      if (!entry.payload_available) details+=" [payload missing from this file]";
      details+='\n';
      if (details.size()>display_limit) break;
    }
    return result(ResourceKind::Archive,summary,details);
  }
  if (b.is(0,"BM")) {
    const auto image=decode_bitmap(b);
    const auto summary="BMP: "+std::to_string(image.width)+" x "+std::to_string(image.height)+".";
    return result(ResourceKind::Bitmap,summary,summary+"\nClose this report to view the image. Drag to pan or use Fit image / Zoom.");
  }
  if (b.is(0,"RIFF") && b.is(8,"WAVE")) {
    const auto wave=parse_wave(b);
    const auto summary="PCM WAVE: "+std::to_string(wave.channels)+" channel(s), "+
        std::to_string(wave.rate)+" Hz, "+std::to_string(wave.bits)+" bit.";
    std::ostringstream details;
    details<<summary<<"\nDuration: "<<double(wave.data_size)/(wave.rate*wave.channels*(wave.bits/8))
           <<" seconds.\nThe Android app starts playback after its browser closes and it regains focus.\n"
           <<"Use Play audio to start/replay or Stop audio to stop. Playback errors appear in the status area.";
    return result(ResourceKind::Wave,summary,details.str());
  }
  if (b.is(8,"RIFF") && b.is(16,"WAVE")) {
    const auto audio=parse_game_audio(b);
    const auto summary=std::string(audio.channels==1 ? "WAC" : "WAS")+": "+
        std::to_string(audio.channels)+" channel(s), "+std::to_string(audio.rate)+" Hz, 16 bit.";
    std::ostringstream details;
    details<<summary<<"\nBlocks: "<<audio.blocks.size()<<"\nDuration: "
           <<double(audio.pcm_size)/(audio.rate*audio.channels*2)<<" seconds.\n"
           <<"Native PCM decoding is available for this header dialect. Unsupported tokens are reported during playback.\n"
           <<"Use Play audio to replay or Stop audio to stop.";
    return result(ResourceKind::CompressedAudio,summary,details.str());
  }
  if (b.is(0,std::string("CNM UNR\0",8)))
    return result(ResourceKind::Unsupported,"Cinema graphics: decoder not implemented.",
        "This is a Cinema/CNM graphics resource.\nThe image/frame decoder is not implemented yet, so this build cannot display it.\n"
        "Some original resources have .bmp names but contain Cinema data. Renaming them does not convert their format.\n"
        "This is a current engine limitation; the app cannot run game scenes yet.");

  // No AQC magic exists in this observed dialect; require the full header model.
  if (b.size()>=56 && b.u32(12)==2 && b.u32(52)==b.u32(4)*2) {
    const auto node=parse_aqc(b);
    const auto summary="AQC static node image: "+std::to_string(node.width)+" x "+std::to_string(node.height)+".";
    const auto details=summary+"\nThe native view offers the verified spherical 360-degree camera for supported angular bounds.\n"
        "Drag to look around, use Zoom, or choose Flat / 360 to inspect the full image. Fit image resets the camera.\n"
        "Trailing bytes not decoded: "+std::to_string(node.tail_size)+
        ". Embedded animations, navigation and hotspots are not implemented.";
    return result(ResourceKind::AqcImage,summary,details);
  }

  // Preserve original single-byte text. NUL/control-rich binary files are not text.
  bool text_file=b.size()>0 && b.size()<=4*1024*1024;
  for (std::size_t i=0;text_file && i<b.size();++i) {
    const auto c=b.u8(i);
    if ((c<32 && c!='\r' && c!='\n' && c!='\t') || c==127) text_file=false;
  }
  if (text_file) {
    const std::string text(reinterpret_cast<const char*>(b.data()),b.size());
    try {
      const auto table=parse_localized(text);
      const auto summary="Localization: "+std::to_string(table.size())+" language records. English labels shown below.";
      std::string details=summary+"\n\n";
      bool english=false;
      for (const auto& entry:table) if (entry.language=="ENG") {
        english=true; details+=entry.key+": "+entry.title;
        if (!entry.text.empty()) details+="\n"+entry.text;
        details+="\n\n";
        if (details.size()>display_limit) break;
      }
      if (!english) {
        details+="No English rows in this table. Available labels:\n";
        for (const auto& entry:table) {
          details+=entry.language+" / "+entry.key+": "+entry.title+" "+entry.text+"\n";
          if (details.size()>display_limit) break;
        }
      }
      return result(ResourceKind::LocalizedText,summary,details);
    } catch (const ParseError&) {}
    try {
      const auto animation=parse_dan(text);
      const auto summary="DAN dialogue animation: "+std::to_string(animation.levels.size())+" level records, "+
          std::to_string(animation.intervals.size())+" intervals.";
      std::string details=summary+"\nTiming units and animation meanings are not yet decoded.\n\nInterval records:\n";
      for (const auto& interval:animation.intervals) {
        details+=std::to_string(interval.first)+" .. "+std::to_string(interval.second)+" : "+std::to_string(interval.third)+"\n";
        if (details.size()>display_limit) break;
      }
      return result(ResourceKind::DialogueAnimation,summary,details);
    } catch (const ParseError&) {}
    return result(ResourceKind::Text,"Text resource: original contents shown below; game semantics are not decoded.",text);
  }
  std::string lower=filename;
  std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return c>='A'&&c<='Z'?char(c-'A'+'a'):char(c);});
  auto ends=[&](const std::string& suffix){return lower.size()>=suffix.size() && lower.compare(lower.size()-suffix.size(),suffix.size(),suffix)==0;};
  std::string family;
  if (ends(".aqc") || lower.find("aqc_sample")!=std::string::npos) family="AQC panorama/image";
  else if (ends(".wac") || ends(".was") || (b.is(8,"RIFF") && b.is(16,"WAVE"))) family="Compressed game audio";
  else if (ends(".ci2")) family="CI2 animation";
  else family="This binary resource";
  const std::string summary=family+": decoder not implemented.";
  return result(ResourceKind::Unsupported,summary,summary+"\n\nNo image, animation, or sound has been decoded from this file.\n"
      "This build can inspect AT3 resource names and localization/DAN text, show standard uncompressed BMPs, and play standard PCM WAVs.\n"
      "Original game scenes and puzzles are not available yet.");
}
}  // namespace jerusalem
