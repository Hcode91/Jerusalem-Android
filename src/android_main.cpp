#include "jerusalem/resources.hpp"
#include "jerusalem/panorama.hpp"
#include "jerusalem/aqc_animation.hpp"
#include "jerusalem/navigation.hpp"
#include "jerusalem/scene_audio.hpp"
#include "jerusalem/interactions.hpp"
#include "jerusalem/checkpoint.hpp"
#include "jerusalem/dialogue.hpp"
#include "jerusalem/cinema.hpp"
#include "jerusalem/stage.hpp"
#include "jerusalem/menu.hpp"
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
#include <iomanip>
#include <utility>

namespace {
std::mutex bridge_mutex;
std::string pending_path;
std::string diagnostic="Native inspector starting. No game scene has been decoded.";
unsigned commands=0;
std::atomic<bool> menu_paused{true},show_hotspots{true},developer_tools{false},topic_requested{false},game_menu_requested{false};
std::atomic<unsigned> game_flags{0},master_volume{100},viewport_bottom{156};
std::string game_result;
std::uint64_t game_result_serial=0;
void result(const std::string& action,bool success){std::lock_guard<std::mutex> guard(bridge_mutex);game_result=std::to_string(++game_result_serial)+":"+action+":"+(success?"ok":"error");}
std::vector<std::pair<std::uint32_t,std::uint32_t>> pending_combinations;
std::string inventory_options,subtitle_report;
std::uint32_t pending_use=0,pending_topic=0;
std::string topic_options;
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
  Uint64 started_=0, duration_ms_=0, paused_at_=0, paused_total_=0;
  bool requested_pause_=false;
  Uint64 base_position_=0;
  bool paused_=false, streaming_=false, replay_button_=true;
  std::uint64_t pcm_bytes_=0;
  std::uint32_t pcm_rate_=0,pcm_frame_bytes_=0;
  bool drained_=false;
  std::string asset_;
  void stop_locked() { if (stream_) SDL_DestroyAudioStream(stream_); stream_=nullptr; drained_=false;streaming_=false; }
 public:
  ~Audio() { stop(); }
  void stop() { std::lock_guard<std::mutex> guard(mutex_);stop_locked(); }
  void pause(bool paused) {
    std::lock_guard<std::mutex> guard(mutex_);requested_pause_=paused;
    if (stream_ && paused_!=paused) {
      const auto now=SDL_GetTicks();if(paused)paused_at_=now;else paused_total_+=now-paused_at_;paused_=paused;
      const bool ok=paused ? SDL_PauseAudioStreamDevice(stream_) : SDL_ResumeAudioStreamDevice(stream_);
      if (!ok) report(std::string("Audio pause/resume failed: ")+SDL_GetError());
    }
  }
  void play(jerusalem::Bytes b,const std::string& name,bool replay_button=true,std::uint64_t offset_ms=0) {
    std::lock_guard<std::mutex> guard(mutex_);
    stop_locked();
    std::vector<std::uint8_t> decoded;
    if (b.is(8,"RIFF") && b.is(16,"WAVE")) {
      decoded=jerusalem::decode_game_audio(b);
      b=jerusalem::Bytes(decoded);
    }
    const auto w=jerusalem::parse_wave(b);
    const auto frame_bytes=unsigned(w.channels)*(w.bits/8);const auto frames=w.data_size/frame_bytes;
    if(!frames)throw jerusalem::ParseError("empty audio track");
    const auto full_duration=(std::uint64_t(frames)*1000+w.rate-1)/w.rate;
    if(offset_ms>full_duration+1000)throw jerusalem::ParseError("saved speech position exceeds duration");
    const auto skip=std::min<std::uint64_t>(frames-1,offset_ms*w.rate/1000)*frame_bytes;
    base_position_=skip/frame_bytes*1000/w.rate;
    if (!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
      throw jerusalem::ParseError(std::string("Audio initialization: ")+SDL_GetError());
    SDL_AudioSpec spec{};
    spec.format=w.bits==8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels=w.channels; spec.freq=static_cast<int>(w.rate);
    stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if (!stream_ || !SDL_PutAudioStreamData(stream_,b.data()+w.data_offset+skip,static_cast<int>(w.data_size-skip)) ||
        !SDL_FlushAudioStream(stream_) || !SDL_SetAudioStreamGain(stream_,master_volume.load()/100.0f) || (!requested_pause_ && !SDL_ResumeAudioStreamDevice(stream_))) {
      const std::string error=SDL_GetError(); stop_locked(); throw jerusalem::ParseError("Audio: "+error);
    }
    asset_=name;replay_button_=replay_button; started_=SDL_GetTicks();paused_=requested_pause_;paused_at_=paused_?started_:0;paused_total_=0;
    duration_ms_=(std::uint64_t(w.data_size-skip)*1000+w.rate*w.channels*(w.bits/8)-1)/(w.rate*w.channels*(w.bits/8));
    const char* driver=SDL_GetCurrentAudioDriver();
    const char* device=SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_));
    __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Audio output: driver=%s device=%s source_bytes=%zu",
                        driver?driver:"unknown",device?device:"unknown",w.data_size);
    report("Playing audio: "+asset_+(replay_button_?". Use Play audio to replay or Stop audio to stop.":". Use Stop audio to stop."));
  }
  void begin_cinema(const jerusalem::CinemaSound& format,const std::string& name) {
    std::lock_guard<std::mutex> guard(mutex_);stop_locked();
    if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))throw jerusalem::ParseError(SDL_GetError());
    SDL_AudioSpec spec{};spec.format=SDL_AUDIO_S16LE;spec.channels=format.channels;spec.freq=static_cast<int>(format.rate);
    stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if(!stream_)throw jerusalem::ParseError(std::string("Cinema audio: ")+SDL_GetError());
    asset_=name;replay_button_=false;streaming_=true;pcm_bytes_=0;pcm_rate_=format.rate;pcm_frame_bytes_=format.channels*2;duration_ms_=0;base_position_=0;started_=SDL_GetTicks();paused_=requested_pause_;paused_at_=paused_?started_:0;paused_total_=0;SDL_SetAudioStreamGain(stream_,master_volume.load()/100.0f);
  }
  void cinema_pcm(const std::vector<std::uint8_t>& pcm) {
    std::lock_guard<std::mutex> guard(mutex_);
    if(!stream_ || !streaming_)throw jerusalem::ParseError("Cinema audio stream unavailable");
    // Valid CNM streams can preload the entire soundtrack, then carry empty Z chunks.
    // SDL3 validates buf before len, so an empty vector must never reach its API.
    if(pcm.empty())return;
    const int queued=SDL_GetAudioStreamQueued(stream_);
    if(queued<0)throw jerusalem::ParseError(std::string("Cinema audio queue: ")+SDL_GetError());
    if(queued>4*1024*1024)throw jerusalem::ParseError("Cinema audio queue exceeds limit");
    if(!SDL_PutAudioStreamData(stream_,pcm.data(),static_cast<int>(pcm.size())))throw jerusalem::ParseError(std::string("Cinema audio submit: ")+SDL_GetError());
    if(!paused_ && !SDL_ResumeAudioStreamDevice(stream_))throw jerusalem::ParseError(std::string("Cinema audio resume: ")+SDL_GetError());
    pcm_bytes_+=pcm.size();duration_ms_=pcm_bytes_*1000/(pcm_rate_*pcm_frame_bytes_);
  }
  void finish_cinema() {
    std::lock_guard<std::mutex> guard(mutex_);if(stream_ && streaming_){if(!SDL_FlushAudioStream(stream_))throw jerusalem::ParseError(SDL_GetError());streaming_=false;}
  }
  std::optional<std::uint64_t> position() {
    std::lock_guard<std::mutex> guard(mutex_);if(!stream_)return std::nullopt;
    return base_position_+(paused_?paused_at_:SDL_GetTicks())-started_-paused_total_;
  }
  void volume(){std::lock_guard<std::mutex> guard(mutex_);if(stream_)SDL_SetAudioStreamGain(stream_,master_volume.load()/100.0f);}
  bool finished() {std::lock_guard<std::mutex> guard(mutex_);return stream_ && drained_ && !streaming_;}
  void update() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!stream_ || drained_ || streaming_) return;
    const int queued=SDL_GetAudioStreamQueued(stream_), available=SDL_GetAudioStreamAvailable(stream_);
    if (queued<0 || available<0) {
      const std::string error=SDL_GetError(); stop_locked(); report("Audio stream error: "+error); return;
    }
    if (queued==0 && available==0 && (paused_?paused_at_:SDL_GetTicks())-started_-paused_total_>=duration_ms_) {
      drained_=true;
      // This verifies delivery to SDL's output device, not audible sound at a speaker.
      report("Audio buffer drained: "+asset_+(replay_button_?". Tap Play audio to replay.":". Playback finished."));
    }
  }
};

class SceneAmbience {
  struct Voice {
    float gain=1;
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
  void volume(){std::lock_guard<std::mutex> guard(mutex_);for(auto& v:voices_)SDL_SetAudioStreamGain(v.second->stream.get(),v.second->gain*master_volume.load()/100.0f);}
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
      if (!SDL_SetAudioStreamGain(voice.stream.get(),(voice.gain=static_cast<float>(jerusalem::ambient_gain(binding.volume,binding.master)))*master_volume.load()/100.0f) ||
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
  bool camera_mode_=false, camera_dirty_=false,game_mode_=false;
  int camera_width_=0,camera_height_=0;
  std::unique_ptr<jerusalem::AqcPreview> animation_;
  std::string animation_error_;
  std::optional<jerusalem::Camera> saved_camera_;
  bool saved_camera_mode_=false;
  Uint64 animation_tick_=0;
  std::vector<jerusalem::Exit> exits_,items_;
  std::vector<jerusalem::StageAccess> puzzle_markers_;
  int width_=0,height_=0;
  float zoom_=1,pan_x_=0,pan_y_=0;
  SDL_FRect logged_puzzle_rect_{};
  int logged_output_width_=0,logged_output_height_=0,logged_bottom_=0;
  std::string preview_;
 public:
  ~Renderer() {
    if (camera_texture_) SDL_DestroyTexture(camera_texture_);
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
  }
  bool init() { return SDL_CreateWindowAndRenderer("Jerusalem",1280,720,SDL_WINDOW_RESIZABLE,&window_,&renderer_); }
  void game_mode(bool on){game_mode_=on;}
  void clear_image() {
    logged_puzzle_rect_={};
    if (texture_) SDL_DestroyTexture(texture_);
    if (camera_texture_) SDL_DestroyTexture(camera_texture_);
    texture_=camera_texture_=nullptr; panorama_={}; camera_mode_=false;
    animation_.reset(); animation_error_.clear(); saved_camera_.reset();
    exits_.clear(); items_.clear();puzzle_markers_.clear();
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
  void puzzle_markers(std::vector<jerusalem::StageAccess> markers){puzzle_markers_=std::move(markers);}
  void items(std::vector<jerusalem::Exit> items) { items_=std::move(items); }
  jerusalem::Camera camera() const { return camera_; }
  jerusalem::Camera checkpoint_camera() const {
    return jerusalem::constrain_camera(geometry_,camera_,camera_width_>0?camera_width_:640,camera_height_>0?camera_height_:448);
  }
  void set_camera(jerusalem::Camera camera) { camera_=camera; camera_dirty_=true; }
  void exits(std::vector<jerusalem::Exit> exits) { exits_=std::move(exits); }
  std::optional<jerusalem::HotspotPoint> hit(float x,float y) const {
    const int bottom=static_cast<int>(viewport_bottom.load());
    int w=0,h=0; SDL_GetRenderOutputSize(renderer_,&w,&h);
    if (!camera_mode_ || !camera_width_ || !camera_height_ || w<=0 || h<=64+bottom || y*h<64 || y*h>=h-bottom) return std::nullopt;
    const auto camera=jerusalem::constrain_camera(geometry_,camera_,camera_width_,camera_height_);
    const auto point=jerusalem::panorama_point(geometry_,camera,2*x-1,2*(y*h-64)/(h-64-bottom)-1,camera_width_,camera_height_);
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
  void panorama_frame(const jerusalem::Bitmap& image) {
    if(!camera_mode_ || image.width!=panorama_.width || image.height!=panorama_.height)throw jerusalem::ParseError("story panorama geometry changed");
    panorama_=image;camera_dirty_=true;
  }
  std::optional<jerusalem::HotspotPoint> puzzle_hit(float x,float y) const {
    const int bottom=static_cast<int>(viewport_bottom.load());
    int w=0,h=0;SDL_GetRenderOutputSize(renderer_,&w,&h);
    return jerusalem::stage_point(x*w,y*h-64,w,h-64-bottom);
  }
  void movie_frame(const jerusalem::Bitmap& image) {
    if(texture_ && !camera_mode_ && width_==static_cast<int>(image.width) && height_==static_cast<int>(image.height)) {
      if(!SDL_UpdateTexture(texture_,nullptr,image.rgba.data(),static_cast<int>(image.width*4)))throw jerusalem::ParseError(SDL_GetError());
    }else bitmap(image);
  }
  void panorama(jerusalem::Bitmap image,const jerusalem::PanoramaGeometry& geometry) {
    bitmap(image);
    panorama_=std::move(image); geometry_=geometry; camera_mode_=true; camera_dirty_=true;
  }
  void draw(const std::string& message) {
    if (animation_ && !menu_paused.load()) {
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
    const int bottom=static_cast<int>(viewport_bottom.load());
    int w=0,h=0; SDL_GetRenderOutputSize(renderer_,&w,&h);
    SDL_SetRenderDrawColor(renderer_,17,23,30,255); SDL_RenderClear(renderer_);
    if (texture_ && w>0 && h>64+bottom) {
      if (camera_mode_) {
        const double scale=std::min(1.0,640.0/std::max(w,h-64-bottom));
        const int rw=std::max(1,static_cast<int>(w*scale)),rh=std::max(1,static_cast<int>((h-64-bottom)*scale));
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
        SDL_FRect dst{0,64,float(w),float(h-64-bottom)};
        SDL_RenderTexture(renderer_,camera_texture_,nullptr,&dst);
        if(!game_mode_ || developer_tools.load() || show_hotspots.load())for (const auto* group:{&exits_,&items_}) for (const auto& exit:*group) if (exit.enabled) {
          const bool item=group==&items_;
          const auto point=jerusalem::hotspot_screen(geometry_,camera_,(exit.left+exit.right)/2.0,
                                                    (exit.top+exit.bottom)/2.0,rw,rh);
          if (!point) continue;
          const float x=float((point->x+1)*w/2),y=64+float((point->y+1)*(h-64-bottom)/2);
          SDL_SetRenderDrawColor(renderer_,item?90:exit.puzzle?240:90,item?200:exit.puzzle?160:230,item?255:100,255);
          SDL_FRect mark{x-14,y-14,28,28}; SDL_RenderRect(renderer_,&mark);
          const std::string label=(item?(exit.cursor==62?"USE ":"ITEM "):exit.puzzle?"PUZZLE ":"EXIT ");
          SDL_RenderDebugText(renderer_,x+18,y-4,(label+(game_mode_&&!developer_tools.load()?std::string{}:std::to_string(exit.target))).c_str());
        }
      } else {
        const float scale=std::min(float(w)/float(width_),float(h-64-bottom)/float(height_))*zoom_;
        SDL_FRect dst{(float(w)-float(width_)*scale)/2+pan_x_*float(w),
                      64+(float(h-64-bottom)-float(height_)*scale)/2+pan_y_*float(h),float(width_)*scale,float(height_)*scale};
        SDL_RenderTexture(renderer_,texture_,nullptr,&dst);
        if(game_mode_ && (dst.x!=logged_puzzle_rect_.x || dst.y!=logged_puzzle_rect_.y || dst.w!=logged_puzzle_rect_.w || dst.h!=logged_puzzle_rect_.h || w!=logged_output_width_ || h!=logged_output_height_ || bottom!=logged_bottom_)){
          logged_puzzle_rect_=dst;logged_output_width_=w;logged_output_height_=h;logged_bottom_=bottom;
          __android_log_print(ANDROID_LOG_INFO,"Jerusalem","A01 puzzle viewport: left=%.3f top=%.3f width=%.3f height=%.3f output=%dx%d bottom=%d image=%dx%d",dst.x,dst.y,dst.w,dst.h,w,h,bottom,width_,height_);
        }
        if(!game_mode_ || developer_tools.load() || show_hotspots.load())for(const auto& a:puzzle_markers_){const float x=dst.x+(a.left+a.right)*0.5f*scale,y=dst.y+(a.top+a.bottom)*0.5f*scale;
          SDL_SetRenderDrawColor(renderer_,90,200,255,255);SDL_FRect mark{x-12,y-12,24,24};SDL_RenderRect(renderer_,&mark);SDL_RenderDebugText(renderer_,x+15,y-4,((a.cursor==61?std::string("ACT "):std::string("USE "))+(game_mode_&&!developer_tools.load()?std::string{}:std::to_string(a.object))).c_str());}
      }
    }
    SDL_SetRenderDrawColor(renderer_,230,235,240,255);
    SDL_RenderDebugText(renderer_,12,12,game_mode_&&!developer_tools.load()?"JERUSALEM":"JERUSALEM / NATIVE ASSET INSPECTOR");
    const std::size_t columns=static_cast<std::size_t>(std::max(16,w/8-4));
    for (std::size_t p=0,line=0;p<message.size()&&line<(game_mode_&&!developer_tools.load()?0U:4U);p+=columns,++line)
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
  std::optional<std::uint32_t> selected_item_;
  std::optional<jerusalem::DialogueText> dialogue_;
  std::optional<jerusalem::Dan> dialogue_animation_;
  std::vector<jerusalem::TopicGroup> keywords_;
  std::vector<std::pair<std::uint32_t,std::uint32_t>> last_speech_layers_;
  std::uint32_t dialogue_id_=0;
  std::unique_ptr<jerusalem::CinemaDecoder> movie_;
  std::uint64_t movie_frames_=0,movie_start_=0,movie_offset_=0,movie_pause_at_=0,movie_paused_total_=0;
  bool movie_paused_=false;
  std::mutex movie_clock_mutex_;
  std::uint64_t movie_time() {std::lock_guard<std::mutex> guard(movie_clock_mutex_);return movie_offset_+(movie_paused_?movie_pause_at_:SDL_GetTicks())-movie_start_-movie_paused_total_;}
  void movie_clock_start(std::uint64_t offset=0){std::lock_guard<std::mutex> guard(movie_clock_mutex_);movie_start_=SDL_GetTicks();movie_offset_=offset;movie_paused_total_=0;if(movie_paused_)movie_pause_at_=movie_start_;}
  std::unique_ptr<jerusalem::A01Story> story_;
  std::unique_ptr<jerusalem::StageArt> story_art_,system_art_;
  std::unique_ptr<jerusalem::AqcAnimation> story_animation_;
  std::map<std::string,jerusalem::Bitmap> art_cache_;
  std::size_t art_cache_bytes_=0;
  struct LayerFrames {jerusalem::StageLayer layer;std::vector<jerusalem::Bitmap> frames;};
  std::vector<LayerFrames> story_layers_;
  jerusalem::Bitmap story_base_;
  bool story_halted_=false;
  std::uint64_t story_seconds_=0,story_assets_=0,resume_media_=0,last_autosave_=0;
  std::uint64_t story_media_=0,story_tick_=0,story_elapsed_=0,story_visual_=0;
  std::atomic<bool> story_reset_clock_{false};
  std::filesystem::path data_root() const {
    const char* storage=SDL_GetAndroidInternalStoragePath();if(!storage)throw jerusalem::ParseError("Android game-data storage unavailable");
    return std::filesystem::path(storage)/"game-data";
  }
  void abandon_story() {art_cache_.clear();art_cache_bytes_=0;topic_requested.store(false);renderer_.game_mode(false);story_.reset();story_art_.reset();system_art_.reset();story_animation_.reset();story_layers_.clear();story_base_={};story_media_=0;}
  void story_error(const std::exception& error) {story_halted_=true;audio_.stop();clear_dialogue();movie_.reset();story_media_=0;report(std::string("A01 story stopped: ")+error.what()+". Tap Start A01 story to restart.");}
  jerusalem::Bitmap art_image(const std::string& name) {
    if(const auto found=art_cache_.find(name);found!=art_cache_.end())return found->second;
    jerusalem::Bitmap image;
    try{image=story_art_->image(name);}catch(const jerusalem::ParseError& e){if(std::string(e.what()).find("missing AT3 image:")!=0)throw;image=system_art_->image(name);}
    if(art_cache_bytes_+image.rgba.size()>48*1024*1024){art_cache_.clear();art_cache_bytes_=0;}
    art_cache_bytes_+=image.rgba.size();art_cache_.emplace(name,image);return image;
  }
  void prepare_story_picture() {
    story_layers_.clear();story_animation_.reset();story_visual_=0;
    if(story_->puzzle()) {
      story_base_={640,480,std::vector<std::uint8_t>(640*480*4,0)};
      const auto& backgrounds=story_->setup().backgrounds;
      const auto b=std::find_if(backgrounds.begin(),backgrounds.end(),[&](const jerusalem::StageBackground& v){return v.id==story_->scene();});
      if(b==backgrounds.end())throw jerusalem::ParseError("missing puzzle background binding");
      jerusalem::stage_blit(story_base_,art_image(b->image),b->x,b->y);
    }else {
      const auto data=jerusalem::read_file(assets_.at(story_->scene()));story_base_=jerusalem::decode_aqc(jerusalem::Bytes(data));
      story_animation_=std::make_unique<jerusalem::AqcAnimation>(data);
    }
    std::size_t memory=0;
    for(const auto& l:story_->layers())if(!l.panorama) {
      LayerFrames frames{l,{}};
      for(unsigned i=0;i<l.frames;++i){std::ostringstream name;name<<l.image;if(l.animated)name<<'.'<<std::setw(4)<<std::setfill('0')<<(i+1)<<(l.channel?".TGA":".BMP");
        auto image=art_image(name.str());memory+=image.rgba.size();if(memory>32*1024*1024)throw jerusalem::ParseError("puzzle layer memory limit");frames.frames.push_back(std::move(image));}
      story_layers_.push_back(std::move(frames));
    }
  }
  void render_story_picture(bool first=false) {
    if(!story_ || story_base_.rgba.empty())return;
    auto image=story_base_;
    if(story_->puzzle()) {
      for(const auto& l:story_layers_){const auto& frame=l.frames.at(jerusalem::stage_frame(story_elapsed_,l.layer));if(l.layer.channel)jerusalem::stage_blit_tga(image,frame,l.layer.x,l.layer.y);else jerusalem::stage_blit(image,frame,l.layer.x,l.layer.y);}
      renderer_.movie_frame(image);renderer_.puzzle_markers(story_->hotspots());
    }else {
      const auto visible=story_->visibility(story_->scene());
      const auto& channels=story_animation_->channels();
      for(std::size_t c=0;c<visible.size();++c)if(visible[c] && channels.at(c).frames.size()==1){const auto& rect=channels[c].frames.front();jerusalem::apply_aqc_patch(image,story_animation_->frame(c,0),rect.left,rect.top);}
      for(const auto& l:story_->layers())if(l.panorama && l.animated){const auto frame=jerusalem::stage_frame(story_elapsed_,l);const auto& rect=channels.at(l.channel).frames.at(frame);jerusalem::apply_aqc_patch(image,story_animation_->frame(l.channel,frame),rect.left,rect.top);}
      if(first){const auto bytes=jerusalem::read_file(assets_.at(story_->scene()));renderer_.panorama(std::move(image),jerusalem::panorama_geometry(jerusalem::Bytes(bytes)));}
      else renderer_.panorama_frame(image);
      std::vector<jerusalem::Exit> exits,markers;
      if(!story_->busy())for(const auto& e:story_->graph().exits)if(e.source==story_->scene())exits.push_back(e);
      for(const auto& a:story_->hotspots())markers.push_back({a.scene,a.index,a.object,false,true,a.left,a.top,a.right,a.bottom,a.cursor,a.event,{}});
      renderer_.exits(std::move(exits));renderer_.items(std::move(markers));
    }
  }
  void start_story_movie(const jerusalem::StoryCue& cue,std::uint64_t offset) {
    const auto path=jerusalem::game_file(data_root(),{"A01","PLA","ENG",cue.asset+".CI2"});
    movie_=std::make_unique<jerusalem::CinemaDecoder>(jerusalem::read_file(path));const auto& info=movie_->info();
    const auto seek=jerusalem::seek_cinema(*movie_,offset);movie_frames_=seek.frames;
    if(!info.sounds.empty()){audio_.begin_cinema(info.sounds.front(),cue.asset);audio_.cinema_pcm(seek.pcm);}
    if(seek.frames)renderer_.movie_frame(movie_->image());
    movie_clock_start(seek.position_ms);report("Playing movie: "+cue.asset);
  }
  void pump_story() {
    if(!story_)return;
    while(const auto cue=story_->next()) {
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","A01 story cue: kind=%u id=%u serial=%llu",unsigned(cue->kind),cue->id,static_cast<unsigned long long>(cue->serial));
      if(cue->kind==jerusalem::StoryCue::Node || cue->kind==jerusalem::StoryCue::Puzzle){
        audio_.stop();clear_dialogue();movie_.reset();node_=cue->kind==jerusalem::StoryCue::Node?std::optional<std::uint32_t>(cue->id):std::nullopt;
        renderer_.clear_image();prepare_story_picture();render_story_picture(true);renderer_.set_camera(cue->camera);publish_inventory();
        if(cue->kind==jerusalem::StoryCue::Node){std::vector<jerusalem::SceneAmbient> bindings;for(const auto& b:navigation_->ambient)if(b.node==cue->id)bindings.push_back(b);ambience_.set(assets_.at(cue->id),bindings);}else{std::vector<jerusalem::SceneAmbient> bindings;for(const auto& b:story_->setup().ambient)if(b.node==cue->id)bindings.push_back(b);ambience_.set(assets_.begin()->second,bindings);}
        report("A01 story: "+std::string(story_->puzzle()?"puzzle ":"node ")+std::to_string(cue->id)+". Tap original hotspots; Inventory selects items.");
      }else if(cue->kind==jerusalem::StoryCue::Movie){
        ambience_.stop();clear_dialogue();audio_.stop();const auto offset=resume_media_;resume_media_=0;
        start_story_movie(*cue,offset);story_media_=cue->serial;break;
      }else if(cue->kind==jerusalem::StoryCue::Speech){story_media_=cue->serial;const auto offset=resume_media_;resume_media_=0;play_dialogue(cue->id,offset);break;}
      else {report(cue->asset);publish_inventory();}
    }
  }
  std::string last_subtitle_;
  void clear_dialogue() {if(dialogue_animation_ && story_)story_->clear_speech_animation(*dialogue_animation_);dialogue_animation_.reset();last_speech_layers_.clear();dialogue_.reset();dialogue_id_=0;last_subtitle_.clear();std::lock_guard<std::mutex> guard(bridge_mutex);subtitle_report.clear();}
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
        const auto name=story_?story_->item_name(id):jerusalem::inventory_name(*navigation_,id);
        out << "- " << name << " [" << id << "]\n";
        options << id << '\t' << name << '\n';
      }
      if(navigation_->combinations.empty())out << "\nCombinations need refreshed connections: long-press A01 navigation and select the original executable again.";
      else out << "\nChoose Combine items to select two ingredients. Use item selects an ingredient for a USE hotspot. Supported character effects and a spoken response are available with refreshed connections.";
      out << (story_?"\nUse Menu → Save game to save story progress. New Game restarts from the opening.":"\nSave checkpoint keeps your node, view, pickups and combinations. Original Windows saves are not supported.");
    }
    std::ostringstream topics;
    if(story_)for(auto id:story_->topics())for(const auto& group:keywords_)for(const auto& entry:group.topics)if(entry.id==id)topics<<id<<'\t'<<entry.text<<'\n';
    std::lock_guard<std::mutex> guard(bridge_mutex);inventory_report=out.str();inventory_options=options.str();topic_options=topics.str();
  }
  void enter(std::uint32_t id,jerusalem::Camera camera,const jerusalem::InteractionState* next=nullptr) {
    selected_item_.reset();clear_dialogue();movie_.reset();
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
    if(state)for(const auto& t:navigation_->targets)if(t.node==id && state->target_enabled(t.id))markers.push_back({id,0,t.id,false,true,t.left,t.top,t.right,t.bottom,t.cursor,t.event,{}});
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
    abandon_story();selected_item_.reset();clear_dialogue();movie_.reset();
    node_.reset(); interactions_.reset(); navigation_.reset(); assets_.clear(); ambience_.stop(); publish_inventory();
    audio_.stop(); renderer_.clear_image(); renderer_.preview("");
    try {
      const auto path=checked_path(requested);
      selected_asset_=path.stem().string();
      for (auto& c:selected_asset_) if (c>='a'&&c<='z') c=char(c-'a'+'A');
      auto data=jerusalem::read_file(path); jerusalem::Bytes b(data);
      const auto description=jerusalem::describe_resource(b,path.filename().string());
      if(description.kind==jerusalem::ResourceKind::Cinema) {
        auto movie=std::make_unique<jerusalem::CinemaDecoder>(std::move(data));
        const auto& info=movie->info();if(!info.rate_word || info.rate_word>10000)throw jerusalem::ParseError("unsupported cinema frame rate");
        if(!info.sounds.empty())audio_.begin_cinema(info.sounds.front(),path.filename().string());
        movie_=std::move(movie);movie_frames_=0;movie_clock_start();renderer_.preview(description.details);report(description.summary);update_movie();return;
      }
      else if (description.kind==jerusalem::ResourceKind::Bitmap) renderer_.bitmap(jerusalem::decode_bitmap(b));
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
  void pause_audio(bool paused) {
    story_reset_clock_.store(true);
    ambience_.pause(paused);std::lock_guard<std::mutex> guard(movie_clock_mutex_);
    if(movie_paused_!=paused){const auto now=SDL_GetTicks();if(paused)movie_pause_at_=now;else movie_paused_total_+=now-movie_pause_at_;movie_paused_=paused;}
  }
  void stop_audio() { ambience_.stop();clear_dialogue();movie_.reset();if(story_)story_error(jerusalem::ParseError("playback cancelled; callbacks were not advanced")); }
  void toggle_navigation(bool reload=false) {
    abandon_story();
    if (node_ && !reload) {
      selected_item_.reset();clear_dialogue();movie_.reset();audio_.stop();node_.reset(); interactions_.reset(); navigation_.reset(); assets_.clear(); renderer_.exits({}); renderer_.items({}); ambience_.stop(); publish_inventory();
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
  bool in_story() const {return bool(story_);}
  bool can_look() const {return !story_ || (!story_->busy() && !story_->puzzle() && !story_halted_);}
  void start_story(bool loading=false,bool automatic=false) {
    try {
      auto prepared=prepare_navigation();const auto bytes=jerusalem::read_file(data_root()/"a01-stage.tsv");
      const auto setup=jerusalem::parse_stage(std::string(bytes.begin(),bytes.end()));
      const auto tags=jerusalem::a01_story_assets(prepared.graph,setup,data_root());const auto tag=tags.current,legacy_tag=tags.legacy;
      auto art=std::make_unique<jerusalem::StageArt>(jerusalem::read_file(jerusalem::game_file(data_root(),{"A01.AT3"})));
      auto system=std::make_unique<jerusalem::StageArt>(jerusalem::read_file(jerusalem::game_file(data_root(),{"SY.AT3"})));
      const auto keyword_bytes=jerusalem::read_file(jerusalem::game_file(data_root(),{"KEYWORDS.INI"}));
      auto keywords=jerusalem::parse_keywords(std::string(keyword_bytes.begin(),keyword_bytes.end()));
      auto items=std::make_unique<jerusalem::InteractionState>(prepared.graph);
      auto next=std::make_unique<jerusalem::A01Story>(setup,prepared.graph,*items,static_cast<std::uint32_t>(SDL_GetTicks()));jerusalem::StoryResume resume;
      if(loading){auto path=story_path(automatic);if(std::filesystem::file_size(path)>65536)throw jerusalem::ParseError("story save size limit");const auto bytes=jerusalem::read_file(path);resume=next->load(jerusalem::Bytes(bytes),tag,legacy_tag);}else next->start();
      abandon_story();audio_.stop();clear_dialogue();movie_.reset();ambience_.stop();node_.reset();selected_item_.reset();
      navigation_=std::move(prepared.graph);assets_=std::move(prepared.assets);interactions_=std::move(items);
      story_art_=std::move(art);system_art_=std::move(system);story_=std::move(next);keywords_=std::move(keywords);topic_requested.store(false);story_assets_=tag;renderer_.game_mode(true);
      story_tick_=SDL_GetTicks();story_elapsed_=resume.visual_ms;story_seconds_=0;story_halted_=false;resume_media_=resume.media_ms;selected_item_=resume.selected;
      renderer_.clear_image();story_base_={};if(story_->scene()){if(!story_->puzzle())node_=story_->scene();prepare_story_picture();render_story_picture(true);renderer_.set_camera(resume.camera);}
      publish_inventory();pump_story();last_autosave_=SDL_GetTicks();result(loading?"load":"new",true);
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Story session: action=%s scene=%u media_ms=%llu",loading?"load":"new",story_->scene(),static_cast<unsigned long long>(resume.media_ms));
      if(!loading)save_story(true);
    }catch(const std::exception& e){
      const std::string reason=e.what();std::string recovery;
      if(reason.find(".tsv")!=std::string::npos || reason.find("refresh A01 connections")!=std::string::npos)
        recovery=". Choose Set up game from the menu and select Jerusalem.ex_.";
      else if(reason.find("missing dialogue")!=std::string::npos || reason.find("missing original game file")!=std::string::npos)
        recovery=". Use Import data to add the original game data containing this resource.";
      report("A01 story unavailable: "+reason+recovery);result(loading?"load":"new",false);
    }
  }
  static std::filesystem::path story_path(bool automatic=false){const char* p=SDL_GetAndroidInternalStoragePath();if(!p)throw jerusalem::ParseError("save storage unavailable");return std::filesystem::path(p)/(automatic?"native-saves/a01-auto.jrs":"native-saves/a01-story.jrs");}
  void save_story(bool automatic=false){
    if(automatic){last_autosave_=SDL_GetTicks();if(!story_ || story_halted_ || story_->failed() || (story_->boundary() && !story_->finished()))return;}
    try{
      if(!story_ || story_halted_)throw jerusalem::ParseError("start a game before saving");
      if(automatic && (story_->failed() || (story_->boundary() && !story_->finished())))return;
      jerusalem::StoryResume resume{renderer_.camera(),0,story_elapsed_,selected_item_};
      if(story_->playing()){resume.media_ms=story_->playing()->kind==jerusalem::StoryCue::Movie?movie_time():audio_.position().value_or(0);}
      const auto bytes=story_->save(resume,story_assets_);jerusalem::write_save_file(story_path(automatic),bytes);last_autosave_=SDL_GetTicks();
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Story checkpoint: action=save auto=%u scene=%u media_ms=%llu",unsigned(automatic),story_->scene(),static_cast<unsigned long long>(resume.media_ms));
      if(!automatic){report("Game saved.");result("save",true);}
    }catch(const std::exception& e){report(std::string(automatic?"Autosave failed: ":"Game save failed: ")+e.what());if(!automatic)result("save",false);}
  }
  void continue_saved(){
    bool automatic=false;try{const auto manual=story_path(),autosave=story_path(true);automatic=std::filesystem::exists(autosave) && (!std::filesystem::exists(manual) || std::filesystem::last_write_time(autosave)>std::filesystem::last_write_time(manual));}catch(const std::exception&){ }
    start_story(true,automatic);
  }
  unsigned flags() const {return story_?(1U | (story_->failed()?2U:0U) | (story_halted_?4U:0U) | (story_->busy()?8U:0U) | (story_->finished()?16U:0U)):0U;}
  void volume(){audio_.volume();ambience_.volume();}
  void update_story() {
    const auto now=SDL_GetTicks();auto delta=now-story_tick_;story_tick_=now;
    if(story_reset_clock_.exchange(false))delta=0;
    if(!story_ || story_halted_)return;
    try {
      story_elapsed_+=delta;story_->tick(delta);pump_story();
      if(now-last_autosave_>=15000)save_story(true);
      const auto seconds=(story_->remaining_ms()+999)/1000;
      if(seconds && seconds!=story_seconds_ && !story_->busy()){story_seconds_=seconds;report("A01 story: "+std::to_string(seconds)+" seconds remain for this attempt.");}
      if(!movie_ && !story_base_.rgba.empty() && story_elapsed_-story_visual_>=80){story_visual_=story_elapsed_;render_story_picture();}
    }catch(const std::exception& e){story_error(e);}
  }
  void story_tap(float x,float y) {
    try {
      if(story_halted_ || story_->busy() || story_->boundary())return;
      if(story_->failed()){game_menu_requested.store(true);return;}
      const auto point=story_->puzzle()?renderer_.puzzle_hit(x,y):renderer_.hit(x,y);if(!point)return;
      if(const auto target=story_->hit(point->x,point->y)) {
        const auto selected=selected_item_;selected_item_.reset();
        if(target->object==10010 && !selected && story_->local_item()) {selected_item_=10010;report("World object selected. Tap the character hotspot to use it.");return;}
        const bool done=!selected && !story_->puzzle() && std::any_of(navigation_->items.begin(),navigation_->items.end(),[&](const jerusalem::Collectible& i){return i.id==target->object;})
          ?story_->pickup(target->object):story_->activate(target->object,target->event,selected);
        if(done){if(target->object==10014 && !story_->topics().empty())topic_requested.store(true);publish_inventory();pump_story();if(!story_->busy()){prepare_story_picture();render_story_picture();}if(!story_->busy())report("A01 interaction accepted at object "+std::to_string(target->object)+".");}
        else report("No supported action for that hotspot and selected item.");
        return;
      }
      selected_item_.reset();if(story_->puzzle())return;
      const auto hit=jerusalem::navigation_hit(story_->graph(),story_->scene(),point->x,point->y);
      if(hit){const auto& exit=story_->graph().exits[*hit];if(exit.puzzle)story_->enter_puzzle(exit.target);else story_->enter_node(exit.target,exit.arrival);pump_story();}
    }catch(const std::exception& e){story_error(e);}
  }
  void update_movie() {
    if(!movie_)return;
    try {
      const auto& info=movie_->info();const auto desired=1+movie_time()*info.rate_word/100000;
      unsigned budget=4;
      while(!movie_->done() && movie_frames_<desired && budget) {
        const auto event=movie_->next();
        if(event.kind==jerusalem::CinemaEvent::Audio && event.channel==0)audio_.cinema_pcm(event.pcm);
        if(event.kind==jerusalem::CinemaEvent::Frame || event.kind==jerusalem::CinemaEvent::Dropped){
          renderer_.movie_frame(movie_->image());++movie_frames_;--budget;
          __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Cinema frame: index=%llu",static_cast<unsigned long long>(movie_frames_-1));
        }
      }
      if(movie_->done()){
        audio_.finish_cinema();
        const auto duration=std::max(std::uint64_t(info.duration_ticks)*10,std::uint64_t(info.frames)*100000/info.rate_word);
        if(story_ && movie_time()<duration)return;
        movie_.reset();
        if(story_){const auto serial=story_media_;story_media_=0;story_->complete(serial);pump_story();
          if(!story_->busy() && !story_->boundary()){prepare_story_picture();render_story_picture();publish_inventory();report("A01 story: "+std::string(story_->puzzle()?"puzzle ":"node ")+std::to_string(story_->scene())+". Tap original hotspots; Inventory selects items.");}
        }
        else report("Cinema preview decoded "+std::to_string(movie_frames_)+" frames. No story event was triggered.");
      }
    }catch(const std::exception& e){movie_.reset();audio_.stop();if(story_)story_error(e);else report(std::string("Cinema preview stopped: ")+e.what());}
  }
  void choose_topic(std::uint32_t id){
    if(!story_ || story_halted_)return;
    try{if(story_->choose_topic(id)){selected_item_.reset();publish_inventory();pump_story();}else report("That conversation topic is not available now.");}
    catch(const std::exception& e){story_error(e);}
  }
  void select_item(std::uint32_t id) {
    if(story_ && (story_halted_ || story_->busy())){report("Wait for the story sequence to finish.");return;}
    if(!navigation_ || !interactions_ || !interactions_->owns(id)){report("Select an item currently in the navigation inventory.");return;}
    if(navigation_->targets.empty()){report("Item use needs refreshed connections: long-press A01 navigation and select the original executable.");return;}
    selected_item_=id;report("Using "+(story_?story_->item_name(id):jerusalem::inventory_name(*navigation_,id))+". Tap a USE marker; an unsupported attempt cancels the selection.");
  }
  void update_dialogue() {
    if(!dialogue_)return;
    const auto elapsed=audio_.position();
    const auto text=elapsed?jerusalem::subtitle_at(*dialogue_,*elapsed):std::string{};
    if(story_ && dialogue_animation_ && elapsed){
      story_->speech_animation(*dialogue_animation_,*elapsed);
      std::vector<std::pair<std::uint32_t,std::uint32_t>> visible;
      for(const auto& l:story_->layers())if(!l.panorama)visible.push_back({l.object,l.presentation});
      if(visible!=last_speech_layers_){last_speech_layers_=std::move(visible);prepare_story_picture();render_story_picture();}
    }
    if(text!=last_subtitle_){last_subtitle_=text;{std::lock_guard<std::mutex> guard(bridge_mutex);subtitle_report=text;}
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Subtitle state: dialogue=%u elapsed=%llu text=%s",dialogue_id_,static_cast<unsigned long long>(elapsed.value_or(0)),text.c_str());}
    if(!elapsed || audio_.finished()){
      clear_dialogue();
      if(story_){try{if(!elapsed)throw jerusalem::ParseError("dialogue playback cancelled");const auto serial=story_media_;story_media_=0;story_->complete(serial);pump_story();}catch(const std::exception& e){story_error(e);}}
    }
  }
  void play_dialogue(std::uint32_t id,std::uint64_t offset=0) {
    const auto& resources=story_?story_->setup().dialogues:navigation_->dialogues;
    const auto found=std::find_if(resources.begin(),resources.end(),[&](const jerusalem::DialogueResource& d){return d.id==id;});
    if(found==resources.end())throw jerusalem::ParseError("unsupported dialogue ID");
    const char* storage=SDL_GetAndroidInternalStoragePath();if(!storage)throw jerusalem::ParseError("Android data storage unavailable");
    const auto files=jerusalem::resolve_dialogue(std::filesystem::path(storage)/"game-data",*found);
    const auto text=jerusalem::read_file(files.subtitles);auto captions=jerusalem::parse_dia(std::string(text.begin(),text.end()));
    const auto sound=jerusalem::read_file(files.audio);audio_.play(jerusalem::Bytes(sound),files.audio.filename().string(),false,offset);
    clear_dialogue();dialogue_=std::move(captions);dialogue_id_=id;
    if(story_ && files.animation){
      try{
        const auto bytes=jerusalem::read_file(*files.animation);auto animation=jerusalem::parse_dan(std::string(bytes.begin(),bytes.end()));
        for(const auto& level:animation.levels)if(std::none_of(story_->setup().layers.begin(),story_->setup().layers.end(),[&](const jerusalem::StageLayer& l){return l.object==level.second && l.presentation==level.third;}))throw jerusalem::ParseError("DAN references an unknown presentation");
        dialogue_animation_=std::move(animation);
      }catch(const jerusalem::ParseError& e){__android_log_print(ANDROID_LOG_WARN,"Jerusalem","Speech animation unavailable: %s: %s",files.animation->filename().string().c_str(),e.what());}
    }
    update_dialogue();
    __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Dialogue started: id=%u stem=%s",id,found->stem.c_str());
  }
  void combine(std::uint32_t first,std::uint32_t second) {
    if(!navigation_ || !interactions_ || (!node_ && !story_)) {report("Start A01 navigation before combining items.");return;}
    if(story_ && (story_halted_ || story_->busy())){report("Wait for the story sequence to finish.");return;}
    try {
      if(navigation_->combinations.empty())throw jerusalem::ParseError("refresh connections by long-pressing A01 navigation and selecting the original executable");
      const auto result=story_?story_->combine(first,second):interactions_->combine(first,second);
      if(!result) {report("These items have no supported combination, or are no longer in inventory.");return;}
      publish_inventory();report("Combined: "+(story_?story_->item_name(*result):jerusalem::inventory_name(*navigation_,*result))+" ["+std::to_string(*result)+"]. Open Inventory to inspect it.");
      __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Combination state: first=%u second=%u result=%u inventory=%zu",first,second,*result,interactions_->inventory().size());
    }catch(const std::exception& e){report(std::string("Combination unavailable: ")+e.what());}
  }
  void save_checkpoint() {
    try {
      if(story_)throw jerusalem::ParseError("use Menu > Save Game for story progress; this control saves navigation previews");
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
      if(story_)throw jerusalem::ParseError("use Menu > Load Game for story progress; this control loads navigation previews");
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
    if(story_){story_tap(x,y);return;}
    if (!node_) return;
    try {
      const auto point=renderer_.hit(x,y);
      if (!point) return;
      if(selected_item_ && interactions_) {
        const auto selected=*selected_item_;selected_item_.reset();
        if(interactions_->hit(*node_,point->x,point->y)){report("Item selection cancelled at a pickup hotspot.");return;}
        const auto target=interactions_->target_hit(*node_,point->x,point->y);
        const auto match=target?interactions_->use_match(*target,selected):std::nullopt;
        if(!match){report("No supported interaction for this item and hotspot. Item selection cancelled.");return;}
        const auto rule=*std::find_if(navigation_->uses.begin(),navigation_->uses.end(),[&](const jerusalem::ItemUse& r){return r.id==*match;});
        auto next=std::make_unique<jerusalem::InteractionState>(*interactions_);
        if(!next->use(*match))return;
        // A response-only rule commits no state; missing speech cannot consume an item.
        if(rule.dialogue)play_dialogue(rule.dialogue);
        else{enter(*node_,renderer_.camera(),next.get());interactions_=std::move(next);report("Item used. Character presentation updated; later story interactions remain unsupported.");}
        __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Item use state: rule=%u target=%u item=%u consumed=%u inventory=%zu",rule.id,rule.target,rule.item,unsigned(rule.consume),interactions_->inventory().size());
        return;
      }
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
      if(interactions_ && interactions_->target_hit(*node_,point->x,point->y)){report("Character hotspot: choose Inventory, Use item, then select an item. Ordinary conversation/puzzle entry is not implemented.");return;}
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
      self.audio_.pause(paused || menu_paused.load()); self.scene_.pause_audio(paused || menu_paused.load());
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

extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeMenuPaused(JNIEnv*,jclass,jboolean paused){menu_paused.store(paused);if(paused)game_menu_requested.store(false);}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeDeveloperMode(JNIEnv*,jclass,jboolean enabled){developer_tools.store(enabled);}
extern "C" JNIEXPORT jint JNICALL
Java_org_jerusalem_engine_MainActivity_nativeGameFlags(JNIEnv*,jclass){return static_cast<jint>(game_flags.load() | (topic_requested.load()?32U:0U) | (game_menu_requested.load()?64U:0U));}
extern "C" JNIEXPORT jstring JNICALL
Java_org_jerusalem_engine_MainActivity_nativeGameResult(JNIEnv* env,jclass){std::lock_guard<std::mutex> guard(bridge_mutex);return env->NewStringUTF(game_result.c_str());}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeViewportBottom(JNIEnv*,jclass,jint pixels){viewport_bottom.store(static_cast<unsigned>(std::clamp(pixels,0,2048)));}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeOptions(JNIEnv*,jclass,jint volume,jboolean markers){master_volume.store(static_cast<unsigned>(std::clamp(volume,0,100)));show_hotspots.store(markers);}
extern "C" JNIEXPORT jstring JNICALL
Java_org_jerusalem_engine_MainActivity_nativeTopics(JNIEnv* env,jclass){topic_requested.store(false);std::lock_guard<std::mutex> guard(bridge_mutex);return env->NewStringUTF(topic_options.c_str());}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeChooseTopic(JNIEnv*,jclass,jint id){if(id>0){std::lock_guard<std::mutex> guard(bridge_mutex);pending_topic=static_cast<std::uint32_t>(id);}}
extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_jerusalem_engine_MainActivity_nativeMenuArt(JNIEnv* env,jclass){
 try{
  const char* storage=SDL_GetAndroidInternalStoragePath();if(!storage)return nullptr;
  const auto art=jerusalem::load_menu_art(jerusalem::StageArt(jerusalem::read_file(jerusalem::game_file(std::filesystem::path(storage)/"game-data",{"SY.AT3"}))));
  const auto type=env->FindClass("[I");if(!type)return nullptr;
  const auto out=env->NewObjectArray(11,type,nullptr);env->DeleteLocalRef(type);if(!out)return nullptr;
  const auto& entries=jerusalem::original_menu_entries();
  for(unsigned slot=0;slot<11;++slot){
    const jerusalem::Bitmap* bitmap=slot==0?&art.background:art.highlights[slot-1]?&*art.highlights[slot-1]:nullptr;
    std::vector<jint> pixels;
    if(slot==0)pixels={0,0,640,480};
    else{const auto& e=entries[slot-1];pixels={e.left,e.top,e.right,e.bottom};}
    pixels.push_back(bitmap?static_cast<jint>(bitmap->width):0);pixels.push_back(bitmap?static_cast<jint>(bitmap->height):0);
    if(bitmap)for(std::size_t i=0;i<bitmap->rgba.size();i+=4)pixels.push_back(static_cast<jint>(unsigned(bitmap->rgba[i+3])<<24 | unsigned(bitmap->rgba[i])<<16 | unsigned(bitmap->rgba[i+1])<<8 | bitmap->rgba[i+2]));
    const auto row=env->NewIntArray(static_cast<jsize>(pixels.size()));if(!row)return nullptr;
    env->SetIntArrayRegion(row,0,static_cast<jsize>(pixels.size()),pixels.data());env->SetObjectArrayElement(out,static_cast<jsize>(slot),row);env->DeleteLocalRef(row);if(env->ExceptionCheck())return nullptr;
  }
  for(const auto& warning:art.warnings)__android_log_print(ANDROID_LOG_WARN,"Jerusalem","Menu highlight unavailable: %s",warning.c_str());
  __android_log_print(ANDROID_LOG_INFO,"Jerusalem","Menu artwork: size=640x480 highlights=%zu",10-art.warnings.size());return out;
 }catch(const std::exception& e){__android_log_print(ANDROID_LOG_WARN,"Jerusalem","Menu artwork unavailable: %s",e.what());return nullptr;}
}
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
extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_jerusalem_engine_MainActivity_nativeExtractStage(JNIEnv* env,jclass,jbyteArray input) {
  try {
    if(!input || env->GetArrayLength(input)!=811008)throw jerusalem::ParseError("select the original executable");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(env->GetArrayLength(input)));
    env->GetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));if(env->ExceptionCheck())return nullptr;
    const auto model=jerusalem::write_stage(jerusalem::extract_a01_stage(jerusalem::Bytes(bytes)));
    const auto result=env->NewByteArray(static_cast<jsize>(model.size()));
    if(result)env->SetByteArrayRegion(result,0,static_cast<jsize>(model.size()),reinterpret_cast<const jbyte*>(model.data()));
    return result;
  }catch(const std::exception& e){const auto type=env->FindClass("java/lang/IllegalArgumentException");if(type)env->ThrowNew(type,e.what());return nullptr;}
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
extern "C" JNIEXPORT jboolean JNICALL
Java_org_jerusalem_engine_MainActivity_nativeSupportsCinema(JNIEnv* env,jclass,jstring path) {
  try{const auto bytes=jerusalem::read_file(checked_path(from_java(env,path)));if(!jerusalem::Bytes(bytes).is(0,std::string("CNM UNR\0",8)))return JNI_FALSE;const jerusalem::CinemaDecoder movie(bytes);return movie.info().frames>1 && movie.info().rate_word>0 && movie.info().rate_word<=10000?JNI_TRUE:JNI_FALSE;}catch(const std::exception&){return JNI_FALSE;}
}
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeCommand(JNIEnv*,jclass,jint command) {
  if (command<1 || command>16) return;
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
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeSelectItem(JNIEnv*,jclass,jint id) {
  if(id<=0)return;
  std::lock_guard<std::mutex> guard(bridge_mutex);pending_use=static_cast<std::uint32_t>(id);
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_jerusalem_engine_MainActivity_nativeSubtitle(JNIEnv* env,jclass) {
  std::lock_guard<std::mutex> guard(bridge_mutex);return env->NewStringUTF(subtitle_report.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_jerusalem_engine_MainActivity_nativeStatus(JNIEnv* env,jclass) {
  std::lock_guard<std::mutex> guard(bridge_mutex); return env->NewStringUTF(diagnostic.c_str());
}

int main(int,char**) {
  game_flags.store(0);
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
    bool running=true,dragging=false,was_menu=false;unsigned last_volume=101;
    float touch_x=0,touch_y=0; bool touch_moved=false;
    std::set<SDL_FingerID> touches;
    SDL_FingerID active_touch=0;
    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type==SDL_EVENT_QUIT) running=false;
        else if (event.type==SDL_EVENT_WILL_ENTER_BACKGROUND) { dragging=false; touches.clear(); touch_moved=true; renderer.reset_animation_clock(); }
        else if (event.type==SDL_EVENT_DID_ENTER_FOREGROUND) { renderer.reset_animation_clock(); }
        else if (menu_paused.load()) {dragging=false;touches.clear();}
        else if (event.type==SDL_EVENT_FINGER_DOWN) {
          if (touches.empty()) { active_touch=event.tfinger.fingerID; touch_x=event.tfinger.x; touch_y=event.tfinger.y; touch_moved=false; }
          else touch_moved=true;
          touches.insert(event.tfinger.fingerID);
        }
        else if (event.type==SDL_EVENT_FINGER_MOTION) {
          if (touches.size()==1 && event.tfinger.fingerID==active_touch) {
            if (std::abs(event.tfinger.x-touch_x)+std::abs(event.tfinger.y-touch_y)>0.015f) touch_moved=true;
            if(scene.can_look())renderer.pan(event.tfinger.dx,event.tfinger.dy);
          }
        }
        else if (event.type==SDL_EVENT_FINGER_UP) {
          if (touches.count(event.tfinger.fingerID) && touches.size()==1 && event.tfinger.fingerID==active_touch && !touch_moved)
            scene.tap(event.tfinger.x,event.tfinger.y);
          touches.erase(event.tfinger.fingerID);
        }
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) dragging=true;
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_UP) dragging=false;
        else if (event.type==SDL_EVENT_MOUSE_MOTION && dragging && scene.can_look()) renderer.pan(event.motion.xrel/1280,event.motion.yrel/720);
        else if (event.type==SDL_EVENT_MOUSE_WHEEL && scene.can_look()) renderer.zoom(event.wheel.y>0?1.2f:1/1.2f);
        else if (event.type==SDL_EVENT_RENDER_DEVICE_RESET) { renderer.clear_image(); report("Graphics device reset. Reopen asset from Browse."); }
      }
      std::string path,message; unsigned action;std::uint32_t use_item,topic;
      std::vector<std::pair<std::uint32_t,std::uint32_t>> combinations;
      {
        std::lock_guard<std::mutex> guard(bridge_mutex);
        use_item=pending_use;pending_use=0;topic=pending_topic;pending_topic=0;path.swap(pending_path);combinations.swap(pending_combinations); action=commands; commands=0; message=diagnostic;
      }
      const bool paused=menu_paused.load() || lifecycle.background();
      if(was_menu!=menu_paused.load()){was_menu=menu_paused.load();audio.pause(paused);scene.pause_audio(paused);renderer.reset_animation_clock();if(was_menu)scene.save_story(true);}
      if(last_volume!=master_volume.load()){last_volume=master_volume.load();scene.volume();}
      if (!path.empty()) { scene.open(path); if (lifecycle.background()) audio.pause(true); }
      if ((action&(1U<<1)) && scene.can_look()) renderer.fit();
      if ((action&(1U<<2)) && scene.can_look()) renderer.zoom(1.25f);
      if ((action&(1U<<3)) && scene.can_look()) renderer.zoom(0.8f);
      if (action&(1U<<4)) { audio.stop(); scene.stop_audio(); report("Audio stopped."); }
      if ((action&(1U<<5)) && !scene.in_story()) renderer.toggle_camera();
      if ((action&(1U<<6)) && !scene.in_story()) renderer.preview_animation();
      if (action&(1U<<7)) scene.toggle_navigation();
      if (action&(1U<<8)) scene.toggle_navigation(true);
      if(use_item)scene.select_item(use_item);
      if(topic)scene.choose_topic(topic);
      for(const auto& pair:combinations)scene.combine(pair.first,pair.second);
      if (action&(1U<<9)) scene.save_checkpoint();
      if (action&(1U<<10)) scene.load_checkpoint();
      if (action&(1U<<11)) scene.start_story();
      if (action&(1U<<12)) scene.save_story();
      if (action&(1U<<13)) scene.start_story(true);
      if (action&(1U<<14)) scene.continue_saved();
      if (action&(1U<<15)) scene.save_story(true);
      if (action&(1U<<16)) scene.start_story(true,true);
      game_flags.store(scene.flags());
      if (!paused) {audio.update();scene.update_dialogue();scene.update_movie();scene.update_story();}
      if (!lifecycle.background()) {
        try { renderer.draw(message); }
        catch (const std::exception& e) { renderer.clear_image(); report(std::string("Render error: ")+e.what()); }
      }
      SDL_Delay(lifecycle.background()?100:16);
    }
  }
  return 0;
}
