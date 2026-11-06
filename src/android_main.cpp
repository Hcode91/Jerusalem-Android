#include "jerusalem/resources.hpp"
#include "jerusalem/panorama.hpp"
#include "jerusalem/aqc_animation.hpp"
#include "jerusalem/navigation.hpp"
#include "jerusalem/scene_audio.hpp"
#include "jerusalem/interactions.hpp"
#include "jerusalem/checkpoint.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <jni.h>
#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <sstream>
#include <utility>

namespace {
std::mutex bridge_mutex;
std::string pending_path;
std::string diagnostic="Native inspector starting. No game scene has been decoded.";
unsigned commands=0;
std::vector<std::pair<std::uint32_t,std::uint32_t>> pending_combinations;
std::string inventory_options;
std::string inventory_report="Start A01 navigation to inspect the pickup inventory.";

void report(const std::string& text) {
  { std::lock_guard<std::mutex> guard(bridge_mutex); diagnostic=text; }
  __android_log_write(ANDROID_LOG_INFO,"Jerusalem",text.c_str());
}
std::string from_java(JNIEnv* env,jstring s) {
  if (!s) throw jerusalem::ParseError("missing asset path");
  const char* raw=env->GetStringUTFChars(s,nullptr);
  if (!raw) throw jerusalem::ParseError("cannot access path string");
  std::string out(raw); env->ReleaseStringUTFChars(s,raw); return out;
}
std::filesystem::path checked_path(const std::string& text) {
  const char* internal=SDL_GetAndroidInternalStoragePath();
  if (!internal) throw jerusalem::ParseError("Android storage is not ready");
  const auto root=std::filesystem::canonical(std::filesystem::path(internal)/"game-data");
  const auto path=std::filesystem::canonical(text);
  const auto relative=path.lexically_relative(root);
  if (relative.empty() || relative.is_absolute() || *relative.begin()==".." || !std::filesystem::is_regular_file(path))
    throw jerusalem::ParseError("asset is outside imported game data");
  return path;
}

class Audio {
  std::mutex mutex_;
  SDL_AudioStream* stream_=nullptr;
  Uint64 started_=0, duration_ms_=0;
  bool drained_=false;
  std::string asset_;
  void stop_locked() { if (stream_) SDL_DestroyAudioStream(stream_); stream_=nullptr; drained_=false; }
 public:
  ~Audio() { stop(); }
  void stop() { std::lock_guard<std::mutex> guard(mutex_);stop_locked(); }
  void pause(bool paused) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (stream_) {
      const bool ok=paused ? SDL_PauseAudioStreamDevice(stream_) : SDL_ResumeAudioStreamDevice(stream_);
      if (!ok) report(std::string("Audio pause/resume failed: ")+SDL_GetError());
    }
  }
  void play(jerusalem::Bytes b,const std::string& name) {
    std::lock_guard<std::mutex> guard(mutex_);
    stop_locked();
    std::vector<std::uint8_t> decoded;
    if (b.is(8,"RIFF") && b.is(16,"WAVE")) {
      decoded=jerusalem::decode_game_audio(b);
      b=jerusalem::Bytes(decoded);
    }
    const auto w=jerusalem::parse_wave(b);
    if (!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
      throw jerusalem::ParseError(std::string("Audio initialization: ")+SDL_GetError());
    SDL_AudioSpec spec{};
    spec.format=w.bits==8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels=w.channels; spec.freq=static_cast<int>(w.rate);
    stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if (!stream_ || !SDL_PutAudioStreamData(stream_,b.data()+w.data_offset,static_cast<int>(w.data_size)) ||
        !SDL_FlushAudioStream(stream_) || !SDL_ResumeAudioStreamDevice(stream_)) {
      const std::string error=SDL_GetError(); stop_locked(); throw jerusalem::ParseError("Audio: "+error);
    }
    asset_=name; started_=SDL_GetTicks();
    duration_ms_=(std::uint64_t(w.data_size)*1000+w.rate*w.channels*(w.bits/8)-1)/(w.rate*w.channels*(w.bits/8));
    const char* driver=SDL_GetCurrentAudioDriver();
    const char* device=SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_));
    __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Audio output: driver=%s device=%s source_bytes=%zu",
                        driver?driver:"unknown",device?device:"unknown",w.data_size);
    report("Playing audio: "+asset_+". Use Play audio to replay or Stop audio to stop.");
  }
  void update() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!stream_ || drained_) return;
    const int queued=SDL_GetAudioStreamQueued(stream_), available=SDL_GetAudioStreamAvailable(stream_);
    if (queued<0 || available<0) {
      const std::string error=SDL_GetError(); stop_locked(); report("Audio stream error: "+error); return;
    }
    if (queued==0 && available==0 && SDL_GetTicks()-started_>=duration_ms_) {
      drained_=true;
      // This verifies delivery to SDL's output device, not audible sound at a speaker.
      report("Audio buffer drained: "+asset_+". Tap Play audio to replay.");
    }
  }
};

class SceneAmbience {
  struct Voice {
    jerusalem::LoopPcm pcm;
    std::filesystem::path path;
    std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> stream{nullptr,SDL_DestroyAudioStream};
    std::uint64_t logged_wraps=0;
    Voice(const std::filesystem::path& asset):pcm(jerusalem::read_file(asset)),path(asset) {
      if (!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
        throw jerusalem::ParseError(std::string("Scene audio initialization: ")+SDL_GetError());
      SDL_AudioSpec spec{}; const auto& w=pcm.wave();
      spec.format=w.bits==8?SDL_AUDIO_U8:SDL_AUDIO_S16LE;spec.channels=w.channels;spec.freq=static_cast<int>(w.rate);
      stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr));
      if (!stream) throw jerusalem::ParseError(std::string("Scene audio stream: ")+SDL_GetError());
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Scene audio opened: %s",path.filename().string().c_str());
    }
    ~Voice() { if (stream) __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Scene audio closed: %s",path.filename().string().c_str()); }
    void fill() {
      for (unsigned i=0;i<4;++i) {
        const int queued=SDL_GetAudioStreamQueued(stream.get()),available=SDL_GetAudioStreamAvailable(stream.get());
        if (queued<0 || available<0) throw jerusalem::ParseError(std::string("Scene audio queue: ")+SDL_GetError());
        if (std::uint64_t(queued)+available>=8192*pcm.frame_bytes()) break;
        auto block=pcm.read(4096);
        if (!SDL_PutAudioStreamData(stream.get(),block.data(),static_cast<int>(block.size())))
          throw jerusalem::ParseError(std::string("Scene audio refill: ")+SDL_GetError());
        if (pcm.wraps()!=logged_wraps) {
          logged_wraps=pcm.wraps();
          __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Scene audio loop: %s wraps=%llu",path.filename().string().c_str(),static_cast<unsigned long long>(logged_wraps));
        }
      }
    }
  };
  std::map<std::uint32_t,std::unique_ptr<Voice>> voices_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_=false,paused_=false;
  std::thread worker_;
  void run() {
    std::unique_lock<std::mutex> guard(mutex_);
    while (!stopping_) {
      if (!paused_) {
        try { for (const auto& item:voices_) item.second->fill(); }
        catch (const std::exception& e) {voices_.clear();report(std::string("Scene audio stopped: ")+e.what());}
      }
      wake_.wait_for(guard,std::chrono::milliseconds(paused_ || voices_.empty()?100:10));
    }
  }
 public:
  SceneAmbience():worker_([this]{run();}) {}
  ~SceneAmbience() {
    {std::lock_guard<std::mutex> guard(mutex_);stopping_=true;wake_.notify_all();}
    worker_.join();
  }
  void stop() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!voices_.empty()) __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Scene audio stopped");
    voices_.clear();
  }
  void pause(bool paused) {
    std::lock_guard<std::mutex> guard(mutex_);paused_=paused;wake_.notify_all();
    if (!voices_.empty()) __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Scene audio %s",paused?"paused":"resumed");
    for (const auto& item:voices_) {
      const bool ok=paused?SDL_PauseAudioStreamDevice(item.second->stream.get()):SDL_ResumeAudioStreamDevice(item.second->stream.get());
      if (!ok) report(std::string("Scene audio pause/resume: ")+SDL_GetError());
    }
  }
  void set(const std::filesystem::path& node,const std::vector<jerusalem::SceneAmbient>& bindings) {
    std::map<std::uint32_t,std::unique_ptr<Voice>> prepared;
    std::size_t bytes=0;
    std::map<std::uint32_t,std::size_t> retained;
    {std::lock_guard<std::mutex> guard(mutex_);for(const auto& item:voices_) retained[item.first]=item.second->pcm.storage_bytes();}
    std::set<std::uint32_t> desired;
    for (const auto& binding:bindings) {
      desired.insert(binding.sound);
      if (!retained.count(binding.sound)) {
        auto voice=std::make_unique<Voice>(checked_path(jerusalem::resolve_ambient_asset(node,binding.stem).string()));
        bytes+=voice->pcm.storage_bytes();
        if (bytes>128*1024*1024) throw jerusalem::ParseError("scene audio exceeds 128 MiB memory limit");
        voice->fill(); prepared.emplace(binding.sound,std::move(voice));
      } else bytes+=retained.at(binding.sound);
      if (bytes>128*1024*1024) throw jerusalem::ParseError("scene audio exceeds 128 MiB memory limit");
    }
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto it=voices_.begin();it!=voices_.end();) {
      if (!desired.count(it->first)) it=voices_.erase(it); else ++it;
    }
    for (auto& item:prepared) voices_.emplace(item.first,std::move(item.second));
    for (const auto& binding:bindings) {
      auto& voice=*voices_.at(binding.sound);
      if (!SDL_SetAudioStreamGain(voice.stream.get(),static_cast<float>(jerusalem::ambient_gain(binding.volume))) ||
          !(paused_?SDL_PauseAudioStreamDevice(voice.stream.get()):SDL_ResumeAudioStreamDevice(voice.stream.get()))) throw jerusalem::ParseError(std::string("Scene audio gain/start: ")+SDL_GetError());
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Scene audio active: %s volume=%u",voice.path.filename().string().c_str(),binding.volume);
    }
  }

};

class Renderer {
  SDL_Window* window_=nullptr;
  SDL_Renderer* renderer_=nullptr;
  SDL_Texture* texture_=nullptr;
  SDL_Texture* camera_texture_=nullptr;
  jerusalem::Bitmap panorama_{};
  jerusalem::PanoramaGeometry geometry_{};
  jerusalem::Camera camera_{};
  bool camera_mode_=false, camera_dirty_=false;
  int camera_width_=0,camera_height_=0;
  std::unique_ptr<jerusalem::AqcPreview> animation_;
  std::string animation_error_;
  std::optional<jerusalem::Camera> saved_camera_;
  bool saved_camera_mode_=false;
  Uint64 animation_tick_=0;
  std::vector<jerusalem::Exit> exits_,items_;
  int width_=0,height_=0;
  float zoom_=1,pan_x_=0,pan_y_=0;
  std::string preview_;
 public:
  ~Renderer() {
    if (camera_texture_) SDL_DestroyTexture(camera_texture_);
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
  }
  bool init() { return SDL_CreateWindowAndRenderer("Jerusalem Asset Inspector",1280,720,SDL_WINDOW_RESIZABLE,&window_,&renderer_); }
  void clear_image() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (camera_texture_) SDL_DestroyTexture(camera_texture_);
    texture_=camera_texture_=nullptr; panorama_={}; camera_mode_=false;
    animation_.reset(); animation_error_.clear(); saved_camera_.reset();
    exits_.clear(); items_.clear();
    camera_width_=camera_height_=0;
  }
  void fit() { zoom_=1; pan_x_=pan_y_=0; camera_={}; camera_dirty_=true; }
  void zoom(float factor) {
    if (camera_mode_) { camera_.fov=std::clamp(camera_.fov/factor,30.0,87.0); camera_dirty_=true; }
    else zoom_=std::clamp(zoom_*factor,0.25f,8.0f);
  }
  void pan(float x,float y) {
    if (camera_mode_) { camera_.yaw-=x*camera_.fov; camera_.pitch-=y*camera_.fov; camera_dirty_=true; }
    else { pan_x_+=x; pan_y_+=y; }
  }
  void toggle_camera() {
    if (panorama_.rgba.empty()) { report("Choose an AQC panorama before switching views."); return; }
    camera_mode_=!camera_mode_; camera_dirty_=true;
    report(camera_mode_ ? "360 panorama view. Drag to look around; Zoom changes the field of view." : "Flat image view. Drag to pan; use Fit image / Zoom.");
  }
  void load_channels(std::vector<std::uint8_t> bytes) {
    if (panorama_.rgba.empty()) { animation_error_="Channel preview requires supported panorama geometry."; return; }
    try { animation_=std::make_unique<jerusalem::AqcPreview>(std::move(bytes),panorama_); }
    catch (const jerusalem::ParseError& e) { animation_error_=e.what(); }
    animation_tick_=SDL_GetTicks();
  }
  void reset_animation_clock() { animation_tick_=SDL_GetTicks(); }
  void items(std::vector<jerusalem::Exit> items) { items_=std::move(items); }
  jerusalem::Camera camera() const { return camera_; }
  jerusalem::Camera checkpoint_camera() const {
    return jerusalem::constrain_camera(geometry_,camera_,camera_width_>0?camera_width_:640,camera_height_>0?camera_height_:448);
  }
  void set_camera(jerusalem::Camera camera) { camera_=camera; camera_dirty_=true; }
  void exits(std::vector<jerusalem::Exit> exits) { exits_=std::move(exits); }
  std::optional<jerusalem::HotspotPoint> hit(float x,float y) const {
    int w=0,h=0; SDL_GetRenderOutputSize(renderer_,&w,&h);
    if (!camera_mode_ || !camera_width_ || !camera_height_ || w<=0 || h<=220 || y*h<64 || y*h>=h-156) return std::nullopt;
    const auto camera=jerusalem::constrain_camera(geometry_,camera_,camera_width_,camera_height_);
    const auto point=jerusalem::panorama_point(geometry_,camera,2*x-1,2*(y*h-64)/(h-220)-1,camera_width_,camera_height_);
    return jerusalem::hotspot_point(geometry_,point);
  }
  void preview_animation() {
    if (!animation_) { report(animation_error_.empty() ? "Open a complete AQC before previewing animation." : animation_error_); return; }
    const bool was_active=animation_->channel().has_value();
    if (animation_->next()) {
      if (!was_active) { saved_camera_=camera_; saved_camera_mode_=camera_mode_; }
      const auto& rect=animation_->selected()->frames.front();
      camera_.yaw=(rect.left+rect.width/2.0)/geometry_.width*360-180;
      camera_.pitch=(rect.top+rect.height/2.0)/geometry_.height*(geometry_.pitch_max-geometry_.pitch_min)+geometry_.pitch_min;
      camera_.fov=30; camera_mode_=true;
      report("Animation preview: channel "+std::to_string(*animation_->channel()+1)+", "+
             std::to_string(animation_->selected()->frames.size())+" frames at 12 fps preview speed. Tap Preview animation to cycle/off. Game triggers are not implemented.");
    } else {
      if (saved_camera_) { camera_=*saved_camera_; camera_mode_=saved_camera_mode_; saved_camera_.reset(); }
      report("Animation preview off. The original static panorama is restored.");
    }
    reset_animation_clock(); camera_dirty_=true;
  }
  void preview(const std::string& text) {
    preview_=text.substr(0,8192);
    // SDL's debug font is ASCII. The selectable Android report retains original bytes.
    for (auto& c:preview_) if ((static_cast<unsigned char>(c)<32 && c!='\n' && c!='\t') || static_cast<unsigned char>(c)>126) c=' ';
  }
  void bitmap(const jerusalem::Bitmap& b) {
    clear_image();
    texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,static_cast<int>(b.width),static_cast<int>(b.height));
    if (!texture_ || !SDL_UpdateTexture(texture_,nullptr,b.rgba.data(),static_cast<int>(b.width*4))) {
      const std::string error=SDL_GetError(); clear_image();
      throw jerusalem::ParseError("Render: "+error);
    }
    width_=static_cast<int>(b.width); height_=static_cast<int>(b.height); fit();
  }
  void panorama(jerusalem::Bitmap image,const jerusalem::PanoramaGeometry& geometry) {
    bitmap(image);
    panorama_=std::move(image); geometry_=geometry; camera_mode_=true; camera_dirty_=true;
  }
  void draw(const std::string& message) {
    if (animation_) {
      const auto now=SDL_GetTicks();
      const auto delta=now-animation_tick_; animation_tick_=now;
      if (animation_->update(panorama_,delta)) {
        if (!SDL_UpdateTexture(texture_,nullptr,panorama_.rgba.data(),width_*4))
          throw jerusalem::ParseError(std::string("Animation upload: ")+SDL_GetError());
        camera_dirty_=true;
        if (animation_->channel())
          __android_log_print(ANDROID_LOG_INFO,"Jerusalem","AQC preview frame: channel=%zu frame=%zu",*animation_->channel(),*animation_->frame());
      }
    }
    int w=0,h=0; SDL_GetRenderOutputSize(renderer_,&w,&h);
    SDL_SetRenderDrawColor(renderer_,17,23,30,255); SDL_RenderClear(renderer_);
    if (texture_ && w>0 && h>220) {
      if (camera_mode_) {
        const double scale=std::min(1.0,640.0/std::max(w,h-220));
        const int rw=std::max(1,static_cast<int>(w*scale)),rh=std::max(1,static_cast<int>((h-220)*scale));
        if (rw!=camera_width_ || rh!=camera_height_) {
          if (camera_texture_) SDL_DestroyTexture(camera_texture_);
          camera_texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,rw,rh);
          if (!camera_texture_) throw jerusalem::ParseError(std::string("Panorama texture: ")+SDL_GetError());
          camera_width_=rw; camera_height_=rh; camera_dirty_=true;
        }
        if (camera_dirty_) {
          camera_=jerusalem::constrain_camera(geometry_,camera_,rw,rh);
          const auto frame=jerusalem::render_panorama(panorama_,geometry_,camera_,rw,rh);
          if (!SDL_UpdateTexture(camera_texture_,nullptr,frame.rgba.data(),rw*4))
            throw jerusalem::ParseError(std::string("Panorama upload: ")+SDL_GetError());
          camera_dirty_=false;
          __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Panorama frame: yaw=%.3f pitch=%.3f fov=%.3f size=%dx%d",camera_.yaw,camera_.pitch,camera_.fov,rw,rh);
        }
        SDL_FRect dst{0,64,float(w),float(h-220)};
        SDL_RenderTexture(renderer_,camera_texture_,nullptr,&dst);
        for (const auto* group:{&exits_,&items_}) for (const auto& exit:*group) if (exit.enabled) {
          const bool item=group==&items_;
          const auto point=jerusalem::hotspot_screen(geometry_,camera_,(exit.left+exit.right)/2.0,
                                                    (exit.top+exit.bottom)/2.0,rw,rh);
          if (!point) continue;
          const float x=float((point->x+1)*w/2),y=64+float((point->y+1)*(h-220)/2);
          SDL_SetRenderDrawColor(renderer_,item?90:exit.puzzle?240:90,item?200:exit.puzzle?160:230,item?255:100,255);
          SDL_FRect mark{x-14,y-14,28,28}; SDL_RenderRect(renderer_,&mark);
          SDL_RenderDebugText(renderer_,x+18,y-4,item?"ITEM":exit.puzzle?"PUZZLE":"EXIT");
        }
      } else {
        const float scale=std::min(float(w)/float(width_),float(h-220)/float(height_))*zoom_;
        SDL_FRect dst{(float(w)-float(width_)*scale)/2+pan_x_*float(w),
                      64+(float(h-220)-float(height_)*scale)/2+pan_y_*float(h),float(width_)*scale,float(height_)*scale};
        SDL_RenderTexture(renderer_,texture_,nullptr,&dst);
      }
    }
    SDL_SetRenderDrawColor(renderer_,230,235,240,255);
    SDL_RenderDebugText(renderer_,12,12,"JERUSALEM / NATIVE ASSET INSPECTOR");
    const std::size_t columns=static_cast<std::size_t>(std::max(16,w/8-4));
    for (std::size_t p=0,line=0;p<message.size()&&line<4;p+=columns,++line)
      SDL_RenderDebugText(renderer_,12,28+float(line)*10,message.substr(p,columns).c_str());
    if (!texture_) {
      std::istringstream lines(preview_); std::string line; float y=76;
      while (y<float(h-150) && std::getline(lines,line)) {
        if (line.empty()) y+=10;
        for (std::size_t p=0;p<line.size() && y<float(h-150);p+=columns,y+=10)
          SDL_RenderDebugText(renderer_,12,y,line.substr(p,columns).c_str());
      }
    }
    SDL_RenderPresent(renderer_);
  }
};

// Traversal uses extracted original connections; story/puzzle callbacks remain separate.
class InspectionScene {
  Renderer& renderer_;
  Audio& audio_;
  SceneAmbience ambience_;
  std::optional<jerusalem::SceneGraph> navigation_;
  std::unique_ptr<jerusalem::InteractionState> interactions_;
  std::map<std::uint32_t,std::filesystem::path> assets_;
  std::optional<std::uint32_t> node_;
  std::string selected_asset_;
  struct NavigationInput {
    jerusalem::SceneGraph graph;
    std::map<std::uint32_t,std::filesystem::path> assets;
  };
  static std::filesystem::path checkpoint_path() {
    const char* path=SDL_GetAndroidInternalStoragePath();
    if(!path)throw jerusalem::ParseError("Android checkpoint storage is not ready");
    return std::filesystem::path(path)/"native-saves/a01-preview.jrs";
  }
  NavigationInput prepare_navigation() {
    const char* storage=SDL_GetAndroidInternalStoragePath();
    if(!storage)throw jerusalem::ParseError("Android game-data storage is not ready");
    const auto root=std::filesystem::path(storage)/"game-data";
    const auto model=jerusalem::read_file(root/"a01-navigation.tsv");
    auto graph=jerusalem::parse_navigation(std::string(model.begin(),model.end()));
    auto assets=jerusalem::resolve_scene_assets(root,graph);
    // The executable supplies fallback labels; optional AOBJ.INI supplies English UI labels.
    for(const auto& file:std::filesystem::directory_iterator(root)) {
      auto name=file.path().filename().string();for(auto& c:name) if(c>='a'&&c<='z') c=char(c-'a'+'A');
      if(name!="AOBJ.INI" || !file.is_regular_file() || file.is_symlink()) continue;
      try {
        const auto data=jerusalem::read_file(file.path());const auto labels=jerusalem::parse_localized(std::string(data.begin(),data.end()));
        for(auto& item:graph.items) for(const auto& label:labels) if(label.key==std::to_string(item.id) && label.language=="ENG" && !label.title.empty() && label.title.size()<96 &&
            std::all_of(label.title.begin(),label.title.end(),[](unsigned char c){return c>=32 && c<=126;})) {item.name=label.title;break;}
        for(auto& c:graph.combinations) for(const auto& label:labels) if(label.key==std::to_string(c.result) && label.language=="ENG" && !label.title.empty() && label.title.size()<96 &&
            std::all_of(label.title.begin(),label.title.end(),[](unsigned char ch){return ch>=32 && ch<=126;})) {c.name=label.title;break;}
      } catch(const std::exception& e) {__android_log_print(ANDROID_LOG_WARN,"Jerusalem","Item labels unavailable: %s",e.what());}
    }
    return {std::move(graph),std::move(assets)};
  }
  void publish_inventory(const jerusalem::InteractionState* state=nullptr) {
    if (!state) state=interactions_.get();
    std::ostringstream out,options;
    if (!navigation_ || !state) out << "Start A01 navigation to view the pickup inventory.";
    else if(navigation_->items.empty()) out << "Pickup support needs refreshed connections: long-press A01 navigation and select the original executable again.";
    else {
      out << "Collected items: " << state->inventory().size() << '\n';
      for (const auto id:state->inventory()) {
        const auto name=jerusalem::inventory_name(*navigation_,id);
        out << "- " << name << " [" << id << "]\n";
        options << id << '\t' << name << '\n';
      }
      if(navigation_->combinations.empty())out << "\nCombinations need refreshed connections: long-press A01 navigation and select the original executable again.";
      else out << "\nChoose Combine items to select two ingredients. Character interactions are not implemented.";
      out << "\nSave checkpoint keeps your node, view, pickups and combinations. Original Windows saves are not supported.";
    }
    std::lock_guard<std::mutex> guard(bridge_mutex);inventory_report=out.str();inventory_options=options.str();
  }
  void enter(std::uint32_t id,jerusalem::Camera camera,const jerusalem::InteractionState* next=nullptr) {
    const auto& node=jerusalem::scene_node(*navigation_,id);
    const auto path=checked_path(assets_.at(id).string());
    auto data=jerusalem::read_file(path); const jerusalem::Bytes bytes(data);
    auto image=jerusalem::decode_aqc(bytes);
    const auto geometry=jerusalem::panorama_geometry(bytes);
    const auto* state=next?next:interactions_.get();
    if (state && !navigation_->items.empty()) image=jerusalem::compose_items(jerusalem::AqcAnimation(data),image,state->visibility(id));
    if (jerusalem::parse_aqc_channels(bytes).size()!=node.channels)
      throw jerusalem::ParseError("scene channel count differs from original setup: "+node.asset);
    audio_.stop(); renderer_.panorama(std::move(image),geometry);
    renderer_.load_channels(std::move(data)); renderer_.set_camera(camera);
    std::vector<jerusalem::Exit> exits;
    for (const auto& exit:navigation_->exits) if (exit.source==id) exits.push_back(exit);
    renderer_.exits(std::move(exits)); node_=id; selected_asset_=node.asset;
    std::vector<jerusalem::Exit> markers;
    if(state) for(const auto& item:navigation_->items) if(item.node==id && !state->collected(item.id))
      markers.push_back({id,0,item.id,false,true,item.left,item.top,item.right,item.bottom,item.cursor,item.event,{}});
    renderer_.items(std::move(markers)); publish_inventory(state);
    report("A01 traversal: "+node.asset+". Drag to look; tap EXIT or ITEM markers. Puzzle/dialogue/story events are not implemented. A01 navigation exits this mode.");
    std::vector<jerusalem::SceneAmbient> bindings;
    for (const auto& binding:navigation_->ambient) if (binding.node==id) bindings.push_back(binding);
    try {
      ambience_.set(path,bindings);
      if (navigation_->ambient.empty()) report("A01 traversal: "+node.asset+". Scene audio needs updated connections: long-press A01 navigation and select the original executable again.");
    } catch (const std::exception& e) { ambience_.stop(); report(std::string("Traversal ready; scene audio unavailable: ")+e.what()); }
  }
 public:
  InspectionScene(Renderer& r,Audio& a):renderer_(r),audio_(a) {}
  void open(const std::string& requested) {
    node_.reset(); interactions_.reset(); navigation_.reset(); assets_.clear(); ambience_.stop(); publish_inventory();
    audio_.stop(); renderer_.clear_image(); renderer_.preview("");
    try {
      const auto path=checked_path(requested);
      selected_asset_=path.stem().string();
      for (auto& c:selected_asset_) if (c>='a'&&c<='z') c=char(c-'a'+'A');
      auto data=jerusalem::read_file(path); jerusalem::Bytes b(data);
      const auto description=jerusalem::describe_resource(b,path.filename().string());
      if (description.kind==jerusalem::ResourceKind::Bitmap) renderer_.bitmap(jerusalem::decode_bitmap(b));
      else if (description.kind==jerusalem::ResourceKind::AqcImage) {
        auto image=jerusalem::decode_aqc(b);
        std::optional<jerusalem::PanoramaGeometry> geometry;
        try { geometry=jerusalem::panorama_geometry(b); }
        catch (const jerusalem::ParseError&) { /* The static image remains supported. */ }
        if (geometry) renderer_.panorama(std::move(image),*geometry);
        else renderer_.bitmap(image);
        renderer_.load_channels(std::move(data));
      }
      else if (description.kind==jerusalem::ResourceKind::Wave || description.kind==jerusalem::ResourceKind::CompressedAudio)
        audio_.play(b,path.filename().string());
      renderer_.preview(description.details);
      if (description.kind!=jerusalem::ResourceKind::Wave && description.kind!=jerusalem::ResourceKind::CompressedAudio)
        report(description.summary);
    } catch (const std::exception& e) { renderer_.preview(e.what()); report(std::string("Asset error: ")+e.what()); }
  }
  void pause_audio(bool paused) { ambience_.pause(paused); }
  void stop_audio() { ambience_.stop(); }
  void toggle_navigation(bool reload=false) {
    if (node_ && !reload) {
      node_.reset(); interactions_.reset(); navigation_.reset(); assets_.clear(); renderer_.exits({}); renderer_.items({}); ambience_.stop(); publish_inventory();
      report("A01 traversal off. Asset inspection controls remain available."); return;
    }
    try {
      auto prepared=prepare_navigation();auto& graph=prepared.graph;auto& assets=prepared.assets;
      auto id=graph.nodes.front().id;
      auto camera=jerusalem::Camera{};
      for (const auto& n:graph.nodes) if (n.asset==selected_asset_) { id=n.id; camera=renderer_.camera(); }
      navigation_=std::move(graph); assets_=std::move(assets);
      interactions_=std::make_unique<jerusalem::InteractionState>(*navigation_);
      enter(id,camera);
    } catch (const std::exception& e) {
      interactions_.reset(); navigation_.reset(); assets_.clear(); node_.reset(); renderer_.exits({}); renderer_.items({}); ambience_.stop(); publish_inventory();
      report(std::string("A01 traversal unavailable: ")+e.what());
    }
  }
  void combine(std::uint32_t first,std::uint32_t second) {
    if(!navigation_ || !interactions_ || !node_) {report("Start A01 navigation before combining items.");return;}
    try {
      if(navigation_->combinations.empty())throw jerusalem::ParseError("refresh connections by long-pressing A01 navigation and selecting the original executable");
      const auto result=interactions_->combine(first,second);
      if(!result) {report("These items have no supported combination, or are no longer in inventory.");return;}
      publish_inventory();report("Combined: "+jerusalem::inventory_name(*navigation_,*result)+" ["+std::to_string(*result)+"]. Open Inventory to inspect it.");
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Combination state: first=%u second=%u result=%u inventory=%zu",first,second,*result,interactions_->inventory().size());
    }catch(const std::exception& e){report(std::string("Combination unavailable: ")+e.what());}
  }
  void save_checkpoint() {
    try {
      if(!node_ || !navigation_ || !interactions_ || navigation_->items.empty())
        throw jerusalem::ParseError("start A01 navigation with pickup connections before saving");
      report("Saving checkpoint...");
      const jerusalem::Checkpoint checkpoint{*node_,renderer_.checkpoint_camera(),interactions_->history()};
      const auto assets=jerusalem::checkpoint_assets(*navigation_,assets_);
      jerusalem::write_checkpoint(checkpoint_path(),*navigation_,assets,checkpoint);
      report("Checkpoint saved at "+jerusalem::scene_node(*navigation_,*node_).asset+" with "+std::to_string(interactions_->inventory().size())+" item(s).");
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Checkpoint state: action=save node=%u yaw=%.6f pitch=%.6f fov=%.6f inventory=%zu",checkpoint.node,checkpoint.camera.yaw,checkpoint.camera.pitch,checkpoint.camera.fov,interactions_->inventory().size());
    }catch(const std::exception& e){report(std::string("Checkpoint save failed: ")+e.what());}
  }
  void load_checkpoint() {
    try {
      report("Loading checkpoint...");
      auto prepared=prepare_navigation();
      const auto tag=jerusalem::checkpoint_assets(prepared.graph,prepared.assets);
      const auto checkpoint=jerusalem::read_checkpoint(checkpoint_path(),prepared.graph,tag);
      auto next=std::make_unique<jerusalem::InteractionState>(jerusalem::restore_interactions(prepared.graph,checkpoint));
      // Invalid/incompatible checkpoint data is rejected before touching the active scene.
      const auto old_node=node_;auto old_selected=std::move(selected_asset_);
      auto old_graph=std::move(navigation_);auto old_assets=std::move(assets_);auto old_state=std::move(interactions_);
      try {
        navigation_=std::move(prepared.graph);assets_=std::move(prepared.assets);
        enter(checkpoint.node,checkpoint.camera,next.get());interactions_=std::move(next);
      }catch(...) {
        navigation_=std::move(old_graph);assets_=std::move(old_assets);interactions_=std::move(old_state);
        node_=old_node;selected_asset_=old_selected;publish_inventory();throw;
      }
      report("Checkpoint loaded at "+jerusalem::scene_node(*navigation_,checkpoint.node).asset+" with "+std::to_string(interactions_->inventory().size())+" item(s).");
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Checkpoint state: action=load node=%u yaw=%.6f pitch=%.6f fov=%.6f inventory=%zu",checkpoint.node,checkpoint.camera.yaw,checkpoint.camera.pitch,checkpoint.camera.fov,interactions_->inventory().size());
    }catch(const std::exception& e){report(std::string("Checkpoint load failed: ")+e.what());}
  }
  void tap(float x,float y) {
    if (!node_) return;
    try {
      const auto point=renderer_.hit(x,y);
      if (!point) return;
      if(interactions_) if(const auto item=interactions_->hit(*node_,point->x,point->y)) {
        auto next=std::make_unique<jerusalem::InteractionState>(*interactions_);
        if(next->pickup(*item)) {
          enter(*node_,renderer_.camera(),next.get());interactions_=std::move(next);
          const auto& object=jerusalem::collectible(*navigation_,*item);
          report("Collected: "+object.name+" ["+std::to_string(*item)+"]. Open Inventory to view it.");
          __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Pickup state: item=%u counter=%u value=%u inventory=%zu",*item,object.counter,unsigned(interactions_->variable(object.counter)),interactions_->inventory().size());
        }
        return;
      }
      const auto hit=jerusalem::navigation_hit(*navigation_,*node_,point->x,point->y);
      if (!hit) { report("No enabled exit at this point. Look for EXIT, ITEM or PUZZLE markers."); return; }
      const auto exit=navigation_->exits[*hit];
      if (exit.puzzle) { report("This hotspot opens an original puzzle. Puzzle interactions are not implemented; no route has been bypassed."); return; }
      enter(exit.target,exit.arrival);
    } catch (const std::exception& e) { report(std::string("Cannot enter scene: ")+e.what()); }
  }
};
// Android may suspend SDL_PollEvent before queued app events reach the loop.
// Handle audio at event enqueue time; renderer/input changes stay on the main loop.
class LifecycleWatch {
  Audio& audio_;
  InspectionScene& scene_;
  std::atomic<bool> background_{false};
  static bool SDLCALL watch(void* context,SDL_Event* event) {
    auto& self=*static_cast<LifecycleWatch*>(context);
    if (event->type==SDL_EVENT_WILL_ENTER_BACKGROUND || event->type==SDL_EVENT_DID_ENTER_FOREGROUND) {
      const bool paused=event->type==SDL_EVENT_WILL_ENTER_BACKGROUND;
      self.background_.store(paused);
      self.audio_.pause(paused); self.scene_.pause_audio(paused);
    }
    return true;
  }
 public:
  LifecycleWatch(Audio& audio,InspectionScene& scene):audio_(audio),scene_(scene) {
    if (!SDL_AddEventWatch(watch,this)) throw jerusalem::ParseError(std::string("Lifecycle event watch: ")+SDL_GetError());
  }
  ~LifecycleWatch() { SDL_RemoveEventWatch(watch,this); }
  bool background() const { return background_.load(); }
};
} // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_jerusalem_engine_MainActivity_nativeDescribe(JNIEnv* env,jclass,jstring path) {
  std::string result;
  try {
    const auto file=checked_path(from_java(env,path));
    const auto data=jerusalem::read_file(file); jerusalem::Bytes b(data);
    result=jerusalem::describe_resource(b,file.filename().string()).details;
  } catch (const std::exception& e) { result=std::string("Cannot inspect asset: ")+e.what(); }
  if (result.size()>128*1024) { result.resize(128*1024); result+="\n[Display truncated]"; }
  auto out=env->NewByteArray(static_cast<jsize>(result.size()));
  if (out) env->SetByteArrayRegion(out,0,static_cast<jsize>(result.size()),reinterpret_cast<const jbyte*>(result.data()));
  return out;
}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeOpen(JNIEnv* env,jclass,jstring path) {
  try { const auto text=from_java(env,path); std::lock_guard<std::mutex> guard(bridge_mutex); pending_path=text; }
  catch (const std::exception& e) { report(e.what()); }
}
extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_jerusalem_engine_MainActivity_nativeExtractA01(JNIEnv* env,jclass,jbyteArray input) {
  try {
    if (!input || env->GetArrayLength(input)>1024*1024) throw jerusalem::ParseError("select Jerusalem.ex_ or Jerusalem.exe, not the disc image");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(env->GetArrayLength(input)));
    env->GetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));
    if (env->ExceptionCheck()) return nullptr;
    const auto model=jerusalem::write_navigation(jerusalem::extract_a01_navigation(jerusalem::Bytes(bytes)));
    const auto result=env->NewByteArray(static_cast<jsize>(model.size()));
    if (result) env->SetByteArrayRegion(result,0,static_cast<jsize>(model.size()),reinterpret_cast<const jbyte*>(model.data()));
    return result;
  } catch (const std::exception& e) {
    const auto type=env->FindClass("java/lang/IllegalArgumentException");
    if (type) env->ThrowNew(type,e.what());
    return nullptr;
  }
}
extern "C" JNIEXPORT jboolean JNICALL
Java_org_jerusalem_engine_MainActivity_nativeSupportsAudio(JNIEnv* env,jclass,jstring path) {
  try {
    const auto file=checked_path(from_java(env,path));
    const auto data=jerusalem::read_file(file);
    jerusalem::Bytes b(data);
    if (b.is(8,"RIFF") && b.is(16,"WAVE")) jerusalem::parse_game_audio(b);
    else jerusalem::parse_wave(b);
    return JNI_TRUE;
  } catch (const std::exception&) { return JNI_FALSE; }
}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeCommand(JNIEnv*,jclass,jint command) {
  if (command<1 || command>10) return;
  std::lock_guard<std::mutex> guard(bridge_mutex); commands|=1U<<command;
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_jerusalem_engine_MainActivity_nativeInventory(JNIEnv* env,jclass) {
  std::lock_guard<std::mutex> guard(bridge_mutex);return env->NewStringUTF(inventory_report.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_jerusalem_engine_MainActivity_nativeInventoryOptions(JNIEnv* env,jclass) {
  std::lock_guard<std::mutex> guard(bridge_mutex);return env->NewStringUTF(inventory_options.c_str());
}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeCombine(JNIEnv*,jclass,jint first,jint second) {
  if(first<=0 || second<=0 || first==second)return;
  std::lock_guard<std::mutex> guard(bridge_mutex);
  if(pending_combinations.size()<8)pending_combinations.emplace_back(first,second);
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_jerusalem_engine_MainActivity_nativeStatus(JNIEnv* env,jclass) {
  std::lock_guard<std::mutex> guard(bridge_mutex); return env->NewStringUTF(diagnostic.c_str());
}

int main(int,char**) {
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
  if (!SDL_Init(SDL_INIT_VIDEO)) { report(SDL_GetError()); SDL_Quit(); return 1; }
  // Declared before renderer/audio so SDL outlives their resource destructors.
  struct SDLGuard { ~SDLGuard() { SDL_Quit(); } } sdl_guard;
  {
    Renderer renderer;
    if (!renderer.init()) { report(SDL_GetError()); return 1; }
    Audio audio;
    const bool have_audio=SDL_InitSubSystem(SDL_INIT_AUDIO);
    report(have_audio ? "Ready. Import data and browse assets. Drag images to pan; use Fit or Zoom controls." :
           std::string("Ready without audio: ")+SDL_GetError());
    InspectionScene scene(renderer,audio);
    LifecycleWatch lifecycle(audio,scene);
    bool running=true,dragging=false;
    float touch_x=0,touch_y=0; bool touch_moved=false;
    std::set<SDL_FingerID> touches;
    SDL_FingerID active_touch=0;
    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type==SDL_EVENT_QUIT) running=false;
        else if (event.type==SDL_EVENT_WILL_ENTER_BACKGROUND) { dragging=false; touches.clear(); touch_moved=true; renderer.reset_animation_clock(); }
        else if (event.type==SDL_EVENT_DID_ENTER_FOREGROUND) { renderer.reset_animation_clock(); }
        else if (event.type==SDL_EVENT_FINGER_DOWN) {
          if (touches.empty()) { active_touch=event.tfinger.fingerID; touch_x=event.tfinger.x; touch_y=event.tfinger.y; touch_moved=false; }
          else touch_moved=true;
          touches.insert(event.tfinger.fingerID);
        }
        else if (event.type==SDL_EVENT_FINGER_MOTION) {
          if (touches.size()==1 && event.tfinger.fingerID==active_touch) {
            if (std::abs(event.tfinger.x-touch_x)+std::abs(event.tfinger.y-touch_y)>0.015f) touch_moved=true;
            renderer.pan(event.tfinger.dx,event.tfinger.dy);
          }
        }
        else if (event.type==SDL_EVENT_FINGER_UP) {
          if (touches.count(event.tfinger.fingerID) && touches.size()==1 && event.tfinger.fingerID==active_touch && !touch_moved)
            scene.tap(event.tfinger.x,event.tfinger.y);
          touches.erase(event.tfinger.fingerID);
        }
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) dragging=true;
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_UP) dragging=false;
        else if (event.type==SDL_EVENT_MOUSE_MOTION && dragging) renderer.pan(event.motion.xrel/1280,event.motion.yrel/720);
        else if (event.type==SDL_EVENT_MOUSE_WHEEL) renderer.zoom(event.wheel.y>0?1.2f:1/1.2f);
        else if (event.type==SDL_EVENT_RENDER_DEVICE_RESET) { renderer.clear_image(); report("Graphics device reset. Reopen asset from Browse."); }
      }
      std::string path,message; unsigned action;
      std::vector<std::pair<std::uint32_t,std::uint32_t>> combinations;
      {
        std::lock_guard<std::mutex> guard(bridge_mutex);
        path.swap(pending_path);combinations.swap(pending_combinations); action=commands; commands=0; message=diagnostic;
      }
      if (!path.empty()) { scene.open(path); if (lifecycle.background()) audio.pause(true); }
      if (action&(1U<<1)) renderer.fit();
      if (action&(1U<<2)) renderer.zoom(1.25f);
      if (action&(1U<<3)) renderer.zoom(0.8f);
      if (action&(1U<<4)) { audio.stop(); scene.stop_audio(); report("Audio stopped."); }
      if (action&(1U<<5)) renderer.toggle_camera();
      if (action&(1U<<6)) renderer.preview_animation();
      if (action&(1U<<7)) scene.toggle_navigation();
      if (action&(1U<<8)) scene.toggle_navigation(true);
      for(const auto& pair:combinations)scene.combine(pair.first,pair.second);
      if (action&(1U<<9)) scene.save_checkpoint();
      if (action&(1U<<10)) scene.load_checkpoint();
      if (!lifecycle.background()) audio.update();
      if (!lifecycle.background()) {
        try { renderer.draw(message); }
        catch (const std::exception& e) { renderer.clear_image(); report(std::string("Render error: ")+e.what()); }
      }
      SDL_Delay(lifecycle.background()?100:16);
    }
  }
  return 0;
}
