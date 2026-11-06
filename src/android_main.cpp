#include "jerusalem/resources.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <jni.h>
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
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
 public:
  ~Audio() { stop(); }
  void stop() { if (stream_) SDL_DestroyAudioStream(stream_); stream_=nullptr; }
  void pause(bool paused) {
    if (stream_) {
      if (paused) SDL_PauseAudioStreamDevice(stream_); else SDL_ResumeAudioStreamDevice(stream_);
    }
  }
  void play(jerusalem::Bytes b) {
    stop(); const auto w=jerusalem::parse_wave(b);
    SDL_AudioSpec spec{};
    spec.format=w.bits==8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels=w.channels; spec.freq=static_cast<int>(w.rate);
    stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if (!stream_ || !SDL_PutAudioStreamData(stream_,b.data()+w.data_offset,static_cast<int>(w.data_size)) ||
        !SDL_FlushAudioStream(stream_) || !SDL_ResumeAudioStreamDevice(stream_)) {
      const std::string error=SDL_GetError(); stop(); throw jerusalem::ParseError("Audio: "+error);
    }
  }
};

class Renderer {
  SDL_Window* window_=nullptr;
  SDL_Renderer* renderer_=nullptr;
  SDL_Texture* texture_=nullptr;
  int width_=0,height_=0;
  float zoom_=1,pan_x_=0,pan_y_=0;
 public:
  ~Renderer() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
  }
  bool init() { return SDL_CreateWindowAndRenderer("Jerusalem Asset Inspector",1280,720,SDL_WINDOW_RESIZABLE,&window_,&renderer_); }
  void clear_image() { if (texture_) SDL_DestroyTexture(texture_); texture_=nullptr; }
  void fit() { zoom_=1; pan_x_=pan_y_=0; }
  void zoom(float factor) { zoom_=std::clamp(zoom_*factor,0.25f,8.0f); }
  void pan(float x,float y) { pan_x_+=x; pan_y_+=y; }
  void bitmap(const jerusalem::Bitmap& b) {
    clear_image();
    texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,static_cast<int>(b.width),static_cast<int>(b.height));
    if (!texture_ || !SDL_UpdateTexture(texture_,nullptr,b.rgba.data(),static_cast<int>(b.width*4))) {
      const std::string error=SDL_GetError(); clear_image();
      throw jerusalem::ParseError("Render: "+error);
    }
    width_=static_cast<int>(b.width); height_=static_cast<int>(b.height); fit();
  }
  void draw(const std::string& message) {
    int w=0,h=0; SDL_GetRenderOutputSize(renderer_,&w,&h);
    SDL_SetRenderDrawColor(renderer_,17,23,30,255); SDL_RenderClear(renderer_);
    if (texture_ && w>0 && h>220) {
      const float scale=std::min(float(w)/float(width_),float(h-220)/float(height_))*zoom_;
      SDL_FRect dst{(float(w)-float(width_)*scale)/2+pan_x_*float(w),
                    64+(float(h-220)-float(height_)*scale)/2+pan_y_*float(h),float(width_)*scale,float(height_)*scale};
      SDL_RenderTexture(renderer_,texture_,nullptr,&dst);
    }
    SDL_SetRenderDrawColor(renderer_,230,235,240,255);
    SDL_RenderDebugText(renderer_,12,12,"JERUSALEM / NATIVE ASSET INSPECTOR");
    const std::size_t columns=static_cast<std::size_t>(std::max(16,w/8-4));
    for (std::size_t p=0,line=0;p<message.size()&&line<4;p+=columns,++line)
      SDL_RenderDebugText(renderer_,12,28+float(line)*10,message.substr(p,columns).c_str());
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
    audio_.stop(); renderer_.clear_image();
    try {
      const auto path=checked_path(requested);
      auto data=jerusalem::read_file(path); jerusalem::Bytes b(data);
      if (b.is(0,"BM")) renderer_.bitmap(jerusalem::decode_bitmap(b));
      else if (b.is(0,"RIFF") && b.is(8,"WAVE")) audio_.play(b);
      report(jerusalem::inspect(b));
    } catch (const std::exception& e) { report(std::string("Asset error: ")+e.what()); }
  }
};
} // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_jerusalem_engine_MainActivity_nativeDescribe(JNIEnv* env,jclass,jstring path) {
  std::string result;
  try {
    const auto file=checked_path(from_java(env,path));
    const auto data=jerusalem::read_file(file); jerusalem::Bytes b(data);
    result=jerusalem::inspect(b)+"\n";
    if (b.is(0,"ATIII")) {
      for (const auto& e:jerusalem::parse_at3(b,true).entries)
        result+=e.name+"\n  offset="+std::to_string(e.offset)+" stored="+std::to_string(e.stored_size)+
          (e.payload_available ? " (present)\n" : " (missing in this prefix)\n");
    } else if (data.size()<=4*1024*1024 && std::find(data.begin(),data.end(),0)==data.end()) {
      const std::string text(data.begin(),data.end());
      bool parsed=false;
      try {
        const auto d=jerusalem::parse_dan(text);
        result+="DAN: "+std::to_string(d.levels.size())+" level records, "+std::to_string(d.intervals.size())+" intervals.\n";
        for (const auto& t:d.intervals) result+=std::to_string(t.first)+".."+std::to_string(t.second)+" = "+std::to_string(t.third)+"\n";
        parsed=true;
      } catch (const jerusalem::ParseError&) {}
      if (!parsed) try {
        const auto localized=jerusalem::parse_localized(text);
        result+="Localization table (original fields; Windows-1252 display):\n";
        for (const auto& e:localized) if (e.language=="ENG") result+=e.key+": "+e.title+" "+e.text+"\n";
      } catch (const jerusalem::ParseError&) {}
    }
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
extern "C" JNIEXPORT void JNICALL
Java_org_jerusalem_engine_MainActivity_nativeCommand(JNIEnv*,jclass,jint command) {
  if (command<1 || command>4) return;
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
      if (!background) renderer.draw(message);
      SDL_Delay(background?100:16);
    }
  }
  return 0;
}
