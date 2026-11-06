#include "jerusalem/resources.hpp"
#include "jerusalem/panorama.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <jni.h>
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>

namespace {
std::mutex bridge_mutex;
std::string pending_path;
std::string diagnostic="Native inspector starting. No game scene has been decoded.";
unsigned commands=0;

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
  SDL_AudioStream* stream_=nullptr;
  Uint64 started_=0, duration_ms_=0;
  bool drained_=false;
  std::string asset_;
 public:
  ~Audio() { stop(); }
  void stop() { if (stream_) SDL_DestroyAudioStream(stream_); stream_=nullptr; drained_=false; }
  void pause(bool paused) {
    if (stream_) {
      const bool ok=paused ? SDL_PauseAudioStreamDevice(stream_) : SDL_ResumeAudioStreamDevice(stream_);
      if (!ok) report(std::string("Audio pause/resume failed: ")+SDL_GetError());
    }
  }
  void play(jerusalem::Bytes b,const std::string& name) {
    stop();
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
      const std::string error=SDL_GetError(); stop(); throw jerusalem::ParseError("Audio: "+error);
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
    if (!stream_ || drained_) return;
    const int queued=SDL_GetAudioStreamQueued(stream_), available=SDL_GetAudioStreamAvailable(stream_);
    if (queued<0 || available<0) {
      const std::string error=SDL_GetError(); stop(); report("Audio stream error: "+error); return;
    }
    if (queued==0 && available==0 && SDL_GetTicks()-started_>=duration_ms_) {
      drained_=true;
      // This verifies delivery to SDL's output device, not audible sound at a speaker.
      report("Audio buffer drained: "+asset_+". Tap Play audio to replay.");
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

// This scene only inspects supported resources. It does not fabricate game navigation.
class InspectionScene {
  Renderer& renderer_;
  Audio& audio_;
 public:
  InspectionScene(Renderer& r,Audio& a):renderer_(r),audio_(a) {}
  void open(const std::string& requested) {
    audio_.stop(); renderer_.clear_image(); renderer_.preview("");
    try {
      const auto path=checked_path(requested);
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
      }
      else if (description.kind==jerusalem::ResourceKind::Wave || description.kind==jerusalem::ResourceKind::CompressedAudio)
        audio_.play(b,path.filename().string());
      renderer_.preview(description.details);
      if (description.kind!=jerusalem::ResourceKind::Wave && description.kind!=jerusalem::ResourceKind::CompressedAudio)
        report(description.summary);
    } catch (const std::exception& e) { renderer_.preview(e.what()); report(std::string("Asset error: ")+e.what()); }
  }
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
  if (command<1 || command>5) return;
  std::lock_guard<std::mutex> guard(bridge_mutex); commands|=1U<<command;
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
    bool running=true,background=false,dragging=false;
    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type==SDL_EVENT_QUIT) running=false;
        else if (event.type==SDL_EVENT_WILL_ENTER_BACKGROUND) { background=true; dragging=false; audio.pause(true); }
        else if (event.type==SDL_EVENT_DID_ENTER_FOREGROUND) { background=false; audio.pause(false); }
        else if (event.type==SDL_EVENT_FINGER_MOTION) renderer.pan(event.tfinger.dx,event.tfinger.dy);
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) dragging=true;
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_UP) dragging=false;
        else if (event.type==SDL_EVENT_MOUSE_MOTION && dragging) renderer.pan(event.motion.xrel/1280,event.motion.yrel/720);
        else if (event.type==SDL_EVENT_MOUSE_WHEEL) renderer.zoom(event.wheel.y>0?1.2f:1/1.2f);
        else if (event.type==SDL_EVENT_RENDER_DEVICE_RESET) { renderer.clear_image(); report("Graphics device reset. Reopen asset from Browse."); }
      }
      std::string path,message; unsigned action;
      {
        std::lock_guard<std::mutex> guard(bridge_mutex);
        path.swap(pending_path); action=commands; commands=0; message=diagnostic;
      }
      if (!path.empty()) { scene.open(path); if (background) audio.pause(true); }
      if (action&(1U<<1)) renderer.fit();
      if (action&(1U<<2)) renderer.zoom(1.25f);
      if (action&(1U<<3)) renderer.zoom(0.8f);
      if (action&(1U<<4)) { audio.stop(); report("Audio stopped."); }
      if (action&(1U<<5)) renderer.toggle_camera();
      if (!background) audio.update();
      if (!background) {
        try { renderer.draw(message); }
        catch (const std::exception& e) { renderer.clear_image(); report(std::string("Render error: ")+e.what()); }
      }
      SDL_Delay(background?100:16);
    }
  }
  return 0;
}
