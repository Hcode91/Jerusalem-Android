#include "jerusalem/resources.hpp"
#include <functional>
#include <iostream>
#include <random>
#include <cstdlib>
using namespace jerusalem;
static int checks=0;
static void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("check failed: "+std::to_string(checks)); }
static void rejects(const std::function<void()>& f) {
  bool rejected=false; try { f(); } catch (const ParseError&) { rejected=true; } check(rejected);
}
static void put(std::vector<std::uint8_t>& b,std::size_t p,std::uint32_t n,int bytes=4) {
  for (int i=0;i<bytes;++i) b.at(p+i)=static_cast<std::uint8_t>(n>>(i*8));
}
static void magic(std::vector<std::uint8_t>& b,std::size_t p,const std::string& s) {
  std::copy(s.begin(),s.end(),b.begin()+static_cast<std::ptrdiff_t>(p));
}
static std::vector<std::uint8_t> wave() {
  std::vector<std::uint8_t> b(48,0); magic(b,0,"RIFF"); put(b,4,40); magic(b,8,"WAVEfmt ");
  put(b,16,16); put(b,20,1,2); put(b,22,1,2); put(b,24,22050); put(b,28,44100);
  put(b,32,2,2); put(b,34,16,2); magic(b,36,"data"); put(b,40,4); return b;
}
static std::vector<std::uint8_t> bmp() {
  std::vector<std::uint8_t> b(62,0); magic(b,0,"BM"); put(b,2,62); put(b,10,54); put(b,14,40);
  put(b,18,1); put(b,22,2); put(b,26,1,2); put(b,28,24,2);
  b[54]=255; b[60]=255; return b; // Bottom blue, top red, row padding is one byte.
}
static std::vector<std::uint8_t> art() {
  std::vector<std::uint8_t> b(291,0); magic(b,0,"ATIII"); put(b,8,255); put(b,12,1); put(b,16,255);
  magic(b,32,"\\image\\synthetic.bmp"); put(b,275,287); put(b,279,4); put(b,283,16);
  magic(b,287,"TEST"); return b;
}
int main() {
  try {
    auto w=wave(); auto info=parse_wave(Bytes(w)); check(info.rate==22050 && info.data_size==4);
    auto stereo=w; put(stereo,22,2,2); put(stereo,28,88200); put(stereo,32,4,2);
    info=parse_wave(Bytes(stereo)); check(info.channels==2 && info.rate==22050 && info.bits==16 && info.data_size==4);
    for (std::size_t i=0;i<w.size();++i) rejects([&] { parse_wave(Bytes(w.data(),i)); });
    auto bad=w; put(bad,40,0xffffffff); rejects([&] { parse_wave(Bytes(bad)); });
    bad=w; put(bad,20,17,2); rejects([&] { parse_wave(Bytes(bad)); });
    bad=w; put(bad,32,1,2); rejects([&] { parse_wave(Bytes(bad)); });
    auto image=bmp(); auto decoded=decode_bitmap(Bytes(image));
    check(decoded.width==1 && decoded.height==2 && decoded.rgba[0]==255 && decoded.rgba[6]==255);
    for (std::size_t i=0;i<image.size();++i) rejects([&] { decode_bitmap(Bytes(image.data(),i)); });
    bad=image; put(bad,22,0xfffffffe); decoded=decode_bitmap(Bytes(bad)); check(decoded.rgba[2]==255);
    bad=image; put(bad,22,0x80000000); rejects([&] { decode_bitmap(Bytes(bad)); });
    bad=image; put(bad,10,0xfffffff0); rejects([&] { decode_bitmap(Bytes(bad)); });
    const auto a=art(); check(parse_at3(Bytes(a)).entries.at(0).stored_size==4);
    for (std::size_t i=0;i<a.size();++i) rejects([&] { parse_at3(Bytes(a.data(),i)); });
    auto partial=parse_at3(Bytes(a.data(),280),true); check(!partial.directory_complete && partial.entries.empty());
    partial=parse_at3(Bytes(a.data(),287),true); check(partial.directory_complete && !partial.entries.at(0).payload_available);
    bad=a; put(bad,275,1); rejects([&] { parse_at3(Bytes(bad),true); });
    bad=a; put(bad,12,0xffffffff); rejects([&] { parse_at3(Bytes(bad),true); });
    bad=a; std::fill(bad.begin()+32,bad.begin()+275,'A'); rejects([&] { parse_at3(Bytes(bad)); });
    auto cue=parse_cue("FILE \"disc.bin\" BINARY\r\n TRACK 01 MODE1/2352\r\n INDEX 01 00:00:00\r\n"); check(cue.binary_name=="disc.bin");
    rejects([] { parse_cue("FILE \"../disc.bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00"); });
    rejects([] { parse_cue("FILE \"disc.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00"); });
    auto ini=parse_ini("; comment\n[game]\nlang=ENG\nlang=FRA\n"); check(ini.size()==2 && ini[1].value=="FRA");
    rejects([] { parse_ini("unknown dialect"); });
    auto loc=parse_localized("42\r\nENG #Synthetic label#\r\nFRA ##\r\n42\r\nENG #Title#Body\r\n");
    check(loc.size()==3 && loc[0].key==loc[2].key && loc[2].text=="Body");
    rejects([] { parse_localized("ENG #missing key#"); });
    rejects([] { parse_localized("key\nENG #missing separator"); });
    auto dan=parse_dan("1\n1 90000 2\n0 5 0\n6 10 1\n11 END\n");
    check(dan.levels.size()==1 && dan.intervals.size()==2 && dan.end==11);
    // Both Android display paths must use decoded content, not the binary header fallback.
    auto describe_text=[](const std::string& text,const std::string& name) {
      return describe_resource(Bytes(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()),name);
    };
    auto description=describe_text("42\nENG #Synthetic label#\n","aObj.ini");
    check(description.kind==ResourceKind::LocalizedText && description.details.find("Synthetic label")!=std::string::npos);
    check(description.summary.find("Header:")==std::string::npos && description.details.find("Header:")==std::string::npos);
    description=describe_text("42\nFRA #Etiquette#\n","aObj.ini");
    check(description.details.find("FRA / 42")!=std::string::npos);
    description=describe_text("1\n1 90000 2\n0 5 0\n6 10 1\n11 END\n","clip.dan");
    check(description.kind==ResourceKind::DialogueAnimation && description.details.find("6 .. 10")!=std::string::npos);
    description=describe_text("[settings]\nvolume=10\n","setup.ini");
    check(description.kind==ResourceKind::Text && description.details.find("volume=10")!=std::string::npos);
    check(describe_resource(Bytes(w),"MENU1.WAV").kind==ResourceKind::Wave);
    check(describe_resource(Bytes(image),"test.bmp").kind==ResourceKind::Bitmap);
    description=describe_resource(Bytes(a),"test.at3");
    check(description.kind==ResourceKind::Archive && description.details.find("synthetic.bmp")!=std::string::npos);
    description=describe_resource(Bytes(a.data(),280),"at3_sample.bin");
    check(description.details.find("truncated directory prefix")!=std::string::npos);
    std::vector<std::uint8_t> cnm{'C','N','M',' ','U','N','R',0};
    description=describe_resource(Bytes(cnm),"original.bmp");
    check(description.kind==ResourceKind::Unsupported && description.summary.find("Cinema")!=std::string::npos);
    std::vector<std::uint8_t> unsupported{0,1,2,3};
    description=describe_resource(Bytes(unsupported),"aqc_sample.bin");
    check(description.kind==ResourceKind::Unsupported && description.details.find("decoder not implemented")!=std::string::npos);
    check(description.details.find("Header:")==std::string::npos);
    rejects([&]{describe_resource(Bytes(w.data(),20),"broken.wav");});
    rejects([] { parse_dan("1\n1 2"); });
    rejects([] { parse_dan("0\n5 2 0\n6 END"); });
    rejects([] { parse_dan("0\n0 4 0\n4 5 1\n6 END"); });
    rejects([] { parse_dan("0\n0 4 0\n4 END"); });
    rejects([] { parse_dan("0\n1 END trailing"); });
    std::vector<std::uint8_t> packed(5,0); packed.insert(packed.end(),w.begin(),w.end()); packed.insert(packed.end(),image.begin(),image.end());
    auto media=scan_media(Bytes(packed)); check(media.size()==2 && media[0].offset==5 && media[1].kind=="bmp");
    bad=w; bad.resize(20); check(scan_media(Bytes(bad)).empty());
    std::mt19937 random(17);
    for (int i=0;i<4000;++i) {
      std::vector<std::uint8_t> noise(random()%512);
      for (auto& c:noise) c=static_cast<std::uint8_t>(random());
      auto attempt=[&](const std::function<void()>& f) { try { f(); } catch (const ParseError&) {} };
      attempt([&]{parse_wave(Bytes(noise));}); attempt([&]{decode_bitmap(Bytes(noise));});
      attempt([&]{parse_at3(Bytes(noise),true);}); attempt([&]{scan_media(Bytes(noise));});
      attempt([&]{decode_game_audio(Bytes(noise));});
    }
    // Optional private regression: no samples are copied into the repository.
    if (const char* path=std::getenv("JERUSALEM_PRIVATE_AT3")) {
      const auto bytes=read_file(path); auto real=parse_at3(Bytes(bytes),true);
      check(real.declared_count==325 && real.entries.size()==15 && !real.directory_complete);
      check(real.entries[0].offset==82907 && real.entries[0].stored_size==7619);
    }
    if (const char* path=std::getenv("JERUSALEM_PRIVATE_DAN")) {
      const auto bytes=read_file(path); const auto real=parse_dan(std::string(bytes.begin(),bytes.end()));
      check(real.levels.size()==1 && real.intervals.size()==8 && real.end==718);
    }
    if (const char* path=std::getenv("JERUSALEM_PRIVATE_AT3_FULL")) {
      const auto bytes=read_file(path); const auto real=parse_at3(Bytes(bytes));
      check(real.directory_complete && real.entries.size()==325);
      for (const auto& entry:real.entries) check(entry.payload_available);
    }
    if (const char* path=std::getenv("JERUSALEM_PRIVATE_WAV")) {
      const auto bytes=read_file(path); const auto real=parse_wave(Bytes(bytes));
      check(real.channels==1 && real.rate==22050 && real.bits==16 && real.data_size==18866);
    }
    if (const char* path=std::getenv("JERUSALEM_PRIVATE_CROWD_WAV")) {
      const auto bytes=read_file(path); const auto real=parse_wave(Bytes(bytes));
      check(real.channels==2 && real.rate==22050 && real.bits==16 && real.data_size==649112);
      check(describe_resource(Bytes(bytes),"a02_crowd.wav").kind==ResourceKind::Wave);
    }
    std::cout << checks << " checks passed; 4000 deterministic malformed-input rounds passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
