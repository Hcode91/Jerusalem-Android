#include "jerusalem/resources.hpp"
#include "jerusalem/panorama.hpp"
#include "jerusalem/aqc_animation.hpp"
#include "jerusalem/navigation.hpp"
#include "jerusalem/checkpoint.hpp"
#include "jerusalem/cinema.hpp"
#include "jerusalem/dialogue.hpp"
#include "jerusalem/stage.hpp"
#include "jerusalem/menu.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>

int main(int argc,char** argv) {
  using namespace jerusalem;
  if (argc<3) {
    std::cerr << "Usage: jinspect describe|inspect|cue|ini|localized|dan|dia|cinema|extract-cinema|cinema-raw|extract-cinema-audio|at3|decode-audio|decode-aqc|aqc-channels|extract-aqc-channels|render-panorama|map-panorama|extract-media|extract-at3|extract-a01-navigation|extract-a01-stage|stage|stage-art|menu|keywords|story-tags|story-checkpoint|checkpoint FILE [NEW_OUTPUT_PATH]\n";
    return 2;
  }
  try {
    auto data=read_file(argv[2]); Bytes b(data);
    const std::string mode=argv[1], text(data.begin(),data.end());
    if (mode=="inspect") std::cout << inspect(b) << '\n';
    else if (mode=="describe") std::cout << describe_resource(b,std::filesystem::path(argv[2]).filename().string()).details << '\n';
    else if(mode=="checkpoint") {
      if(argc!=5)throw ParseError("checkpoint FILE MODEL DATA_ROOT");
      const auto input=read_file(argv[3]);const auto graph=parse_navigation(std::string(input.begin(),input.end()));
      const auto assets=resolve_scene_assets(argv[4],graph);const auto cp=decode_checkpoint(b,graph,checkpoint_assets(graph,assets));
      const auto state=restore_interactions(graph,cp);
      std::cout << "Native A01 checkpoint: node=" << cp.node << " asset=" << scene_node(graph,cp.node).asset
                << " yaw=" << cp.camera.yaw << " pitch=" << cp.camera.pitch << " fov=" << cp.camera.fov << '\n';
      for(const auto id:state.inventory())std::cout << "item\t" << id << '\t' << inventory_name(graph,id) << '\n';
    }
    else if(mode=="dia") {
      const auto captions=parse_dia(text);for(const auto& cue:captions.cues)std::cout << cue.time_ms << '\t' << cue.text << '\t' << cue.secondary << '\n';std::cout << captions.end_ms << "\tEND\n";
    }
    else if(mode=="cinema" || mode=="extract-cinema" || mode=="cinema-raw" || mode=="extract-cinema-audio") {
      if(mode!="cinema" && argc!=4)throw ParseError("cinema extraction requires a new output directory");
      CinemaDecoder decoder(data);const auto& info=decoder.info();std::filesystem::path root;std::ofstream raw;
      if(mode=="cinema-raw"){if(std::filesystem::exists(argv[3]))throw ParseError("raw output must be new");raw.open(argv[3],std::ios::binary);if(!raw)throw ParseError("cannot create raw output");}
      if(mode=="extract-cinema" || mode=="extract-cinema-audio"){root=argv[3];if(!std::filesystem::create_directory(root))throw ParseError("output directory already exists");}
      std::cout << "CNM " << info.width << 'x' << info.height << " frames=" << info.frames << " rate_word=" << info.rate_word << " duration_ticks=" << info.duration_ticks << '\n';
      unsigned count=0;std::uint64_t exported=0;
      std::vector<std::vector<std::uint8_t>> pcm(info.sounds.size());
      while(!decoder.done()) {
        const auto event=decoder.next();
        if(event.kind==CinemaEvent::Audio){auto& channel=pcm.at(event.channel);if(channel.size()+event.pcm.size()>max_resource_bytes)throw ParseError("cinema audio export exceeds 64 MiB");channel.insert(channel.end(),event.pcm.begin(),event.pcm.end());}
        if(event.kind==CinemaEvent::Frame || event.kind==CinemaEvent::Dropped) {
          std::uint64_t hash=14695981039346656037ULL;for(auto c:decoder.dib())hash=(hash^c)*1099511628211ULL;
          std::cout << "frame\t" << count << '\t' << std::hex << hash << std::dec << '\n';
          if(raw.is_open()){exported+=decoder.dib().size();if(exported>1024ULL*1024*1024)throw ParseError("cinema raw export exceeds 1 GiB");raw.write(reinterpret_cast<const char*>(decoder.dib().data()),static_cast<std::streamsize>(decoder.dib().size()));if(!raw)throw ParseError("cannot write raw cinema frame");}
          if(mode=="extract-cinema") {const auto image=decoder.image();exported+=std::uint64_t(image.width)*image.height*3;if(exported>1024ULL*1024*1024)throw ParseError("cinema image export exceeds 1 GiB");std::ofstream out(root/(std::to_string(count)+".ppm"),std::ios::binary);out << "P6\n" << image.width << ' ' << image.height << "\n255\n";for(std::size_t i=0;i<image.rgba.size();i+=4)out.write(reinterpret_cast<const char*>(image.rgba.data()+i),3);if(!out)throw ParseError("cannot write cinema frame");}
          ++count;
        }
      }
      if(!root.empty())for(std::size_t c=0;c<pcm.size();++c) {
        const auto& sound=info.sounds[c];const auto size=static_cast<std::uint32_t>(pcm[c].size());
        std::ofstream out(root/("audio-"+std::to_string(c)+".wav"),std::ios::binary);
        const auto word=[&](std::uint32_t value,unsigned width){for(unsigned i=0;i<width;++i)out.put(char(value>>(8*i)));};
        out.write("RIFF",4);word(36+size,4);out.write("WAVEfmt ",8);word(16,4);word(1,2);word(sound.channels,2);word(sound.rate,4);word(sound.rate*sound.channels*2,4);word(sound.channels*2,2);word(16,2);out.write("data",4);word(size,4);out.write(reinterpret_cast<const char*>(pcm[c].data()),size);if(!out)throw ParseError("cannot write cinema PCM wave");
      }
      std::cout << "Decoded " << count << " frame(s); progressive frames; fps=rate_word/100, duration_ms=duration_ticks*10; interlaced unsupported\n";
    }
    else if(mode=="stage") {
      const auto setup=parse_stage(text);std::cout<<setup.backgrounds.size()<<" backgrounds, "<<setup.access.size()<<" hotspots, "<<setup.layers.size()<<" layers, "<<setup.dialogues.size()<<" voice bindings\n";
    }
    else if(mode=="story-checkpoint") {
      if(argc!=6)throw ParseError("story-checkpoint NAVIGATION STAGE GAME_ROOT SAVE");
      const auto graph=parse_navigation(text);const auto raw=read_file(argv[3]);const auto setup=parse_stage(std::string(raw.begin(),raw.end()));
      const auto tags=a01_story_assets(graph,setup,argv[4]);InteractionState items(graph);A01Story story(setup,graph,items);
      const auto state=read_file(argv[5]);story.load(Bytes(state),tags.current,tags.legacy);
      std::cout<<"Story scene="<<story.scene()<<" finished="<<story.finished()<<" inventory="<<items.inventory().size()<<'\n';
    }
    else if(mode=="story-tags") {
      if(argc!=5)throw ParseError("story-tags NAVIGATION STAGE GAME_ROOT");
      const auto stage=read_file(argv[3]);const auto tags=a01_story_assets(parse_navigation(text),parse_stage(std::string(stage.begin(),stage.end())),argv[4]);
      std::cout<<tags.current<<' '<<tags.legacy<<'\n';
    }
    else if(mode=="keywords") {
      const auto groups=parse_keywords(text);std::cout<<groups.size()<<" topic groups\n";
      for(const auto& group:groups)for(const auto& topic:group.topics)std::cout<<group.id<<'\t'<<group.root<<'\t'<<topic.id<<'\t'<<topic.text<<'\n';
    }
    else if(mode=="menu") {
      const auto menu=load_menu_art(StageArt(std::move(data)));
      std::cout<<"Original menu: "<<menu.background.width<<'x'<<menu.background.height<<", "<<10-menu.warnings.size()<<" highlights\n";
      const auto& entries=original_menu_entries();
      for(const auto& entry:entries)std::cout<<entry.object<<'\t'<<entry.label<<'\t'<<entry.left<<','<<entry.top<<','<<entry.right<<','<<entry.bottom<<'\t'<<entry.highlight<<'\n';
      for(const auto& warning:menu.warnings)std::cout<<"Warning: "<<warning<<'\n';
    }
    else if(mode=="stage-art") {
      if(argc!=5 || std::filesystem::exists(argv[4]))throw ParseError("stage-art ARCHIVE BASENAME NEW_OUTPUT.pam");
      const auto image=StageArt(data).image(argv[3]);std::ofstream out(argv[4],std::ios::binary);
      out<<"P7\nWIDTH "<<image.width<<"\nHEIGHT "<<image.height<<"\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
      out.write(reinterpret_cast<const char*>(image.rgba.data()),static_cast<std::streamsize>(image.rgba.size()));if(!out)throw ParseError("cannot write stage art");
    }
    else if(mode=="extract-a01-stage") {
      const auto model=write_stage(extract_a01_stage(b));
      if(argc!=4 || std::filesystem::exists(argv[3]))throw ParseError("stage extraction requires a new output file");
      std::ofstream out(argv[3]);out<<model;if(!out)throw ParseError("cannot write stage model");
      std::cout<<"Original A01 puzzle layouts, layers and voice bindings extracted\n";
    }
    else if (mode=="extract-a01-navigation") {
      if (argc!=4) throw ParseError("navigation extraction requires a new output directory");
      const auto graph=extract_a01_navigation(b);
      const std::filesystem::path root=argv[3];
      if (!std::filesystem::create_directory(root)) throw ParseError("output directory must not already exist");
      std::ofstream out(root/"a01-navigation.tsv"); out << write_navigation(graph);
      if (!out) throw ParseError("cannot write native navigation model");
      std::cout << graph.nodes.size() << " nodes, " << graph.exits.size() << " exits, " << graph.ambient.size() << " ambient bindings, " << graph.items.size() << " collectibles, " << graph.combinations.size() << " combinations extracted without executing the program\n";
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
