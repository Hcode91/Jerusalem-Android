#include "jerusalem/resources.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>

int main(int argc,char** argv) {
  using namespace jerusalem;
  if (argc<3) {
    std::cerr << "Usage: jinspect describe|inspect|cue|ini|localized|dan|at3|decode-audio|decode-aqc|extract-media|extract-at3 FILE [NEW_OUTPUT_PATH]\n";
    return 2;
  }
  try {
    auto data=read_file(argv[2]); Bytes b(data);
    const std::string mode=argv[1], text(data.begin(),data.end());
    if (mode=="inspect") std::cout << inspect(b) << '\n';
    else if (mode=="describe") std::cout << describe_resource(b,std::filesystem::path(argv[2]).filename().string()).details << '\n';
    else if (mode=="cue") std::cout << parse_cue(text).binary_name << "\tMODE1/2352\n";
    else if (mode=="ini") for (const auto& e:parse_ini(text))
      std::cout << e.line << '\t' << e.section << '\t' << e.key << '\t' << e.value << '\n';
    else if (mode=="localized") for (const auto& e:parse_localized(text))
      std::cout << e.key << '\t' << e.language << '\t' << e.title << '\t' << e.text << '\n';
    else if (mode=="dan") {
      const auto d=parse_dan(text);
      for (const auto& e:d.levels) std::cout << "level\t" << e.first << '\t' << e.second << '\t' << e.third << '\n';
      for (const auto& e:d.intervals) std::cout << "interval\t" << e.first << '\t' << e.second << '\t' << e.third << '\n';
      std::cout << "end\t" << d.end << '\n';
    } else if (mode=="at3") {
      const auto art=parse_at3(b,true);
      std::cout << "directory_complete=" << art.directory_complete << " available=" << art.entries.size() << " declared=" << art.declared_count << '\n';
      for (const auto& e:art.entries)
        std::cout << e.name << '\t' << e.offset << '\t' << e.stored_size << '\t' << e.auxiliary_size << '\t' << e.payload_available << '\n';
    } else if (mode=="decode-aqc") {
      if (argc!=4) throw ParseError("AQC conversion requires a new output directory");
      const auto info=parse_aqc(b);
      const auto image=decode_aqc(b);
      const std::filesystem::path root=argv[3];
      if (!std::filesystem::create_directory(root)) throw ParseError("output directory must not already exist");
      std::ofstream out(root/"decoded.ppm",std::ios::binary);
      out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
      for (std::size_t i=0;i<image.rgba.size();i+=4)
        out.write(reinterpret_cast<const char*>(image.rgba.data()+i),3);
      if (!out) throw ParseError("failed writing decoded AQC image");
      std::cout << "Static image decoded; " << info.tail_size << " trailing bytes remain undecoded\n";
    } else if (mode=="decode-audio") {
      if (argc!=4) throw ParseError("audio conversion requires a new output directory");
      const auto wave=decode_game_audio(b);
      const std::filesystem::path root=argv[3];
      if (!std::filesystem::create_directory(root)) throw ParseError("output directory must not already exist");
      std::ofstream out(root/"decoded.wav",std::ios::binary);
      out.write(reinterpret_cast<const char*>(wave.data()),static_cast<std::streamsize>(wave.size()));
      if (!out) throw ParseError("failed writing decoded audio");
      std::cout << wave.size() << " PCM WAVE bytes written to " << (root/"decoded.wav") << '\n';
    } else if (mode=="extract-media" || mode=="extract-at3") {
      if (argc!=4) throw ParseError("extraction requires a new output directory");
      std::vector<Embedded> entries;
      if (mode=="extract-at3") {
        for (const auto& e:parse_at3(b).entries) entries.push_back({"stored",e.offset,e.stored_size});
      } else entries=scan_media(b);
      if (entries.empty()) throw ParseError("no structurally valid supported media found");
      const std::filesystem::path root=argv[3];
      if (!std::filesystem::create_directory(root)) throw ParseError("output directory must not already exist");
      std::ofstream manifest(root/"manifest.tsv");
      if (!manifest) throw ParseError("cannot create extraction manifest");
      for (std::size_t i=0;i<entries.size();++i) {
        const auto& e=entries[i];
        const auto name=std::to_string(i)+"."+e.kind;
        std::ofstream out(root/name,std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()+e.offset),static_cast<std::streamsize>(e.size));
        if (!out) throw ParseError("failed writing extracted data");
        manifest << name << '\t' << e.offset << '\t' << e.size << '\n';
      }
      if (!manifest) throw ParseError("failed writing manifest");
      std::cout << entries.size() << " stored object(s) extracted; no proprietary decompression performed\n";
    } else throw ParseError("unknown command");
    return 0;
  } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
