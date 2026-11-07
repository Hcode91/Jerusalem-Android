#include "jerusalem/dialogue.hpp"
#include <algorithm>
#include <sstream>
namespace jerusalem {
namespace {
std::string utf8(const std::string& bytes) {
  static constexpr unsigned special[]={0x20ac,0,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0,0x17d,0,0,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0,0x17e,0x178};
  std::string out;
  for(unsigned char c:bytes) {
    unsigned value=c==0xa0?' ':c;
    if(c>=0x80 && c<0xa0)value=special[c-0x80];
    if(!value || value<32 || value==127)throw ParseError("unsupported subtitle control/encoding byte");
    if(value<0x80)out+=char(value);
    else if(value<0x800){out+=char(0xc0|(value>>6));out+=char(0x80|(value&63));}
    else{out+=char(0xe0|(value>>12));out+=char(0x80|((value>>6)&63));out+=char(0x80|(value&63));}
  }
  return out;
}
std::filesystem::path child(const std::filesystem::path& root,const std::string& name,bool directory) {
  std::optional<std::filesystem::path> found;
  for(const auto& file:std::filesystem::directory_iterator(root)) {
    auto current=file.path().filename().string();for(auto& c:current)if(c>='a'&&c<='z')c=char(c-'a'+'A');
    if(current!=name)continue;
    if(found || file.is_symlink() || (directory?!file.is_directory():!file.is_regular_file()))throw ParseError("ambiguous/unsafe dialogue path component");
    found=file.path();
  }
  if(!found)throw ParseError("missing dialogue data: "+name);
  return *found;
}
}
DialogueText parse_dia(const std::string& bytes) {
  if(bytes.empty() || bytes.size()>64*1024 || bytes.find('\0')!=std::string::npos)throw ParseError("invalid DIA text size/embedded NUL");
  DialogueText result{};std::istringstream lines(bytes);std::string line;bool ended=false;std::uint32_t last=0;
  while(std::getline(lines,line)) {
    if(!line.empty() && line.back()=='\r')line.pop_back();
    std::size_t p=line.find_first_not_of(" \t");if(p==std::string::npos)continue;
    if(ended || line.size()>4096 || result.cues.size()>=512)throw ParseError("DIA trailing text/line limit");
    std::uint64_t time=0;const auto begin=p;
    while(p<line.size() && line[p]>='0' && line[p]<='9'){time=time*10+unsigned(line[p++]-'0');if(time>24*60*60*1000)throw ParseError("DIA timestamp limit");}
    if(p==begin || p==line.size() || (line[p]!=' ' && line[p]!='\t'))throw ParseError("unsupported DIA timestamp dialect");
    p=line.find_first_not_of(" \t",p);if(p==std::string::npos)throw ParseError("missing DIA caption");
    const auto text=line.substr(p);const auto separator=text.find('#');
    const auto first=text.substr(0,separator),second=separator==std::string::npos?std::string{}:text.substr(separator+1);
    if(!result.cues.empty() && time<=last)throw ParseError("unordered/repeated DIA timestamp");
    last=static_cast<std::uint32_t>(time);
    if(first=="END" && second.empty()){if(result.cues.empty())throw ParseError("DIA has no captions");result.end_ms=last;ended=true;}
    else result.cues.push_back({last,utf8(first),utf8(second)});
  }
  if(!ended)throw ParseError("truncated DIA: missing terminal END");
  return result;
}
std::string subtitle_at(const DialogueText& text,std::uint64_t time) {
  // The original chooses the first inclusive interval; the earlier cue wins at equality.
  for(std::size_t i=0;i<text.cues.size();++i)if(text.cues[i].time_ms<=time && time<=(i+1<text.cues.size()?text.cues[i+1].time_ms:text.end_ms))return text.cues[i].text;
  return {};
}
DialogueFiles resolve_dialogue(const std::filesystem::path& root,const DialogueResource& d) {
  SceneGraph validation;validation.nodes={{1,"VALIDATE",0}};validation.dialogues={d};validate_navigation(validation);
  if(std::filesystem::is_symlink(root))throw ParseError("unsafe dialogue root");
  // Data imports may preserve DATA or flatten it, just like node resolution.
  std::optional<std::filesystem::path> area;
  for(const auto& candidate:{root,root/"data",root/"DATA"})if(std::filesystem::is_directory(candidate) && !std::filesystem::is_symlink(candidate)) {
    try{const auto selected=child(candidate,d.area,true);if(area && *area!=selected)throw ParseError("ambiguous dialogue area");area=selected;}catch(const ParseError& e){if(std::string(e.what()).find("missing dialogue data:")!=0)throw;}
  }
  if(!area)throw ParseError("missing dialogue area: "+d.area);
  const auto sound=child(child(*area,"SOUND",true),"ENG",true),dia=child(child(*area,"DIA",true),"ENG",true);
  // Executable registrations can name .WAC while the disc stores .WAS
  // (A01's ISHT_DIA001, for example). Decoding is selected from the bytes.
  std::optional<std::filesystem::path> audio;
  for(const auto* suffix:{".WAC",".WAS",".WAV"}) {
    std::optional<std::filesystem::path> candidate;
    try {candidate=child(sound,d.stem+suffix,false);}
    catch(const ParseError& e) {if(std::string(e.what()).find("missing dialogue data:")!=0)throw;}
    if(candidate) {
      if(audio)throw ParseError("ambiguous dialogue audio: "+d.stem+" has multiple WAC/WAS/WAV files");
      audio=std::move(candidate);
    }
  }
  if(!audio)throw ParseError("missing dialogue audio: "+d.stem+" (expected WAC, WAS or WAV in "+sound.string()+")");
  return {*audio,child(dia,d.stem+".DIA",false)};
}
}
