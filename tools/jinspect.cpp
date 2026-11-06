#include "jerusalem/resources.hpp"
#include "jerusalem/panorama.hpp"
#include "jerusalem/aqc_animation.hpp"
#include "jerusalem/navigation.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>

int main(int argc,char** argv) {
  using namespace jerusalem;
  if (argc<3) {
    std::cerr << "Usage: jinspect describe|inspect|cue|ini|localized|dan|at3|decode-audio|decode-aqc|aqc-channels|extract-aqc-channels|render-panorama|map-panorama|extract-media|extract-at3|extract-a01-navigation FILE [NEW_OUTPUT_PATH]\n";
    return 2;
  }
  try {
    auto data=read_file(argv[2]); Bytes b(data);
    const std::string mode=argv[1], text(data.begin(),data.end());
    if (mode=="inspect") std::cout << inspect(b) << '\n';
    else if (mode=="describe") std::cout << describe_resource(b,std::filesystem::path(argv[2]).filename().string()).details << '\n';
    else if (mode=="extract-a01-navigation") {
      if (argc!=4) throw ParseError("navigation extraction requires a new output directory");
      const auto graph=extract_a01_navigation(b);
      const std::filesystem::path root=argv[3];
      if (!std::filesystem::create_directory(root)) throw ParseError("output directory must not already exist");
      std::ofstream out(root/"a01-navigation.tsv"); out << write_navigation(graph);
      if (!out) throw ParseError("cannot write native navigation model");
      std::cout << graph.nodes.size() << " nodes, " << graph.exits.size() << " exits, " << graph.ambient.size() << " ambient bindings, " << graph.items.size() << " collectibles extracted without executing the program\n";
    }
    else if (mode=="map-panorama") {
      if (argc!=10) throw ParseError("map-panorama FILE YAW PITCH FOV WIDTH HEIGHT COLUMNS ROWS");
      const auto geometry=panorama_geometry(b);
      const auto width=std::stoul(argv[6]),height=std::stoul(argv[7]),cols=std::stoul(argv[8]),rows=std::stoul(argv[9]);
      if (!width || width>4096 || !height || height>4096 || cols<2 || cols>64 || rows<2 || rows>64)
        throw ParseError("invalid panorama mapping dimensions");
      const auto camera=constrain_camera(geometry,{std::stod(argv[3]),std::stod(argv[4]),std::stod(argv[5])},
                                         static_cast<unsigned>(width),static_cast<unsigned>(height));
      std::cout << std::setprecision(12) << "camera," << camera.yaw << ',' << camera.pitch << ',' << camera.fov << '\n';
      for (unsigned y=0;y<rows;++y) for (unsigned x=0;x<cols;++x) {
        const auto point=panorama_point(geometry,camera,2.0*x/(cols-1)-1,2.0*y/(rows-1)-1,
                                        static_cast<unsigned>(width),static_cast<unsigned>(height));
        std::cout << point.x << ',' << point.y << '\n';
      }
    }
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
    } else if (mode=="aqc-channels" || mode=="extract-aqc-channels") {
      const AqcAnimation animation(std::move(data));
      if (mode=="aqc-channels") {
        for (std::size_t c=0;c<animation.channels().size();++c) {
          const auto& channel=animation.channels()[c];
          std::cout << "channel\t" << c << "\tframes\t" << channel.frames.size() << "\ttiming_value\t" << channel.timing_value << '\n';
        }
      } else {
        if (argc!=4) throw ParseError("channel extraction requires a new output directory");
        std::uint64_t total=0;
        for (const auto& channel:animation.channels()) for (const auto& frame:channel.frames)
          total+=std::uint64_t(frame.width)*frame.height*3;
        if (total>512*1024*1024) throw ParseError("512 MiB decoded RGB export limit exceeded");
        const std::filesystem::path root=argv[3];
        if (!std::filesystem::create_directory(root)) throw ParseError("output directory must not already exist");
        std::ofstream manifest(root/"manifest.tsv");
        if (!manifest) throw ParseError("cannot create channel manifest");
        manifest << "channel\tframe\tleft\ttop\twidth\theight\ttiming_value\n";
        std::size_t count=0;
        for (std::size_t c=0;c<animation.channels().size();++c) {
          const auto& channel=animation.channels()[c];
          for (std::size_t f=0;f<channel.frames.size();++f) {
            const auto patch=animation.frame(c,f);
            const auto name="channel-"+std::to_string(c)+"-frame-"+std::to_string(f)+".ppm";
            std::ofstream out(root/name,std::ios::binary);
            out << "P6\n" << patch.width << ' ' << patch.height << "\n255\n";
            for (std::size_t i=0;i<patch.rgba.size();i+=4) out.write(reinterpret_cast<const char*>(patch.rgba.data()+i),3);
            if (!out) throw ParseError("failed writing AQC patch");
            const auto& rect=channel.frames[f];
            manifest << c << '\t' << f << '\t' << rect.left << '\t' << rect.top << '\t'
                     << rect.width << '\t' << rect.height << '\t' << channel.timing_value << '\n';
            ++count;
          }
        }
        if (!manifest) throw ParseError("failed writing channel manifest");
        std::cout << count << " original patch frame(s) extracted; no game activation/timing implemented\n";
      }
    } else if (mode=="decode-aqc" || mode=="render-panorama") {
      if (argc!=4 && !(mode=="render-panorama" && argc==7))
        throw ParseError("image conversion requires a new output directory; render-panorama optionally accepts YAW PITCH FOV");
      const auto info=parse_aqc(b);
      auto image=decode_aqc(b);
      if (mode=="render-panorama") {
        Camera camera;
        if (argc==7) camera={std::stod(argv[4]),std::stod(argv[5]),std::stod(argv[6])};
        image=render_panorama(image,panorama_geometry(b),camera,640,448);
      }
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
