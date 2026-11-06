#include "jerusalem/resources.hpp"
#include "jerusalem/panorama.hpp"
#include "jerusalem/aqc_animation.hpp"
#include "jerusalem/navigation.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <jni.h>
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

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
  std::unique_ptr<jerusalem::AqcPreview> animation_;
  std::string animation_error_;
  std::optional<jerusalem::Camera> saved_camera_;
  bool saved_camera_mode_=false;
  Uint64 animation_tick_=0;
  std::vector<jerusalem::Exit> exits_;
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
    exits_.clear();
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
  jerusalem::Camera camera() const { return camera_; }
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
        for (const auto& exit:exits_) if (exit.enabled) {
          const auto point=jerusalem::hotspot_screen(geometry_,camera_,(exit.left+exit.right)/2.0,
                                                    (exit.top+exit.bottom)/2.0,rw,rh);
          if (!point) continue;
          const float x=float((point->x+1)*w/2),y=64+float((point->y+1)*(h-220)/2);
          SDL_SetRenderDrawColor(renderer_,exit.puzzle?240:90,exit.puzzle?160:230,100,255);
          SDL_FRect mark{x-14,y-14,28,28}; SDL_RenderRect(renderer_,&mark);
          SDL_RenderDebugText(renderer_,x+18,y-4,exit.puzzle?"PUZZLE":"EXIT");
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

// This scene only inspects supported resources. It does not fabricate game navigation.
class InspectionScene {
  Renderer& renderer_;
  Audio& audio_;
  std::optional<jerusalem::SceneGraph> navigation_;
  std::map<std::uint32_t,std::filesystem::path> assets_;
  std::optional<std::uint32_t> node_;
  std::string selected_asset_;
  void enter(std::uint32_t id,jerusalem::Camera camera) {
    const auto& node=jerusalem::scene_node(*navigation_,id);
    const auto path=checked_path(assets_.at(id).string());
    auto data=jerusalem::read_file(path); const jerusalem::Bytes bytes(data);
    auto image=jerusalem::decode_aqc(bytes);
    const auto geometry=jerusalem::panorama_geometry(bytes);
    if (jerusalem::parse_aqc_channels(bytes).size()!=node.channels)
      throw jerusalem::ParseError("scene channel count differs from original setup: "+node.asset);
    audio_.stop(); renderer_.panorama(std::move(image),geometry);
    renderer_.load_channels(std::move(data)); renderer_.set_camera(camera);
    std::vector<jerusalem::Exit> exits;
    for (const auto& exit:navigation_->exits) if (exit.source==id) exits.push_back(exit);
    renderer_.exits(std::move(exits)); node_=id; selected_asset_=node.asset;
    report("A01 traversal: "+node.asset+". Drag to look; tap EXIT markers. Puzzle/dialogue/story events are not implemented. A01 navigation exits this mode.");
  }
 public:
  InspectionScene(Renderer& r,Audio& a):renderer_(r),audio_(a) {}
  void open(const std::string& requested) {
    node_.reset(); navigation_.reset(); assets_.clear();
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
  void toggle_navigation(bool reload=false) {
    if (node_ && !reload) {
      node_.reset(); navigation_.reset(); assets_.clear(); renderer_.exits({});
      report("A01 traversal off. Asset inspection controls remain available."); return;
    }
    try {
      const auto root=std::filesystem::path(SDL_GetAndroidInternalStoragePath())/"game-data";
      const auto model=jerusalem::read_file(root/"a01-navigation.tsv");
      auto graph=jerusalem::parse_navigation(std::string(model.begin(),model.end()));
      auto assets=jerusalem::resolve_scene_assets(root,graph);
      auto id=graph.nodes.front().id;
      auto camera=jerusalem::Camera{};
      for (const auto& n:graph.nodes) if (n.asset==selected_asset_) { id=n.id; camera=renderer_.camera(); }
      navigation_=std::move(graph); assets_=std::move(assets);
      enter(id,camera);
    } catch (const std::exception& e) {
      navigation_.reset(); assets_.clear(); node_.reset(); renderer_.exits({});
      report(std::string("A01 traversal unavailable: ")+e.what());
    }
  }
  void tap(float x,float y) {
    if (!node_) return;
    try {
      const auto point=renderer_.hit(x,y);
      if (!point) return;
      const auto hit=jerusalem::navigation_hit(*navigation_,*node_,point->x,point->y);
      if (!hit) { report("No enabled exit at this point. Look for EXIT or PUZZLE markers."); return; }
      const auto exit=navigation_->exits[*hit];
      if (exit.puzzle) { report("This hotspot opens an original puzzle. Puzzle interactions are not implemented; no route has been bypassed."); return; }
      enter(exit.target,exit.arrival);
    } catch (const std::exception& e) { report(std::string("Cannot enter scene: ")+e.what()); }
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
  if (command<1 || command>8) return;
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
    float touch_x=0,touch_y=0; bool touch_moved=false;
    std::set<SDL_FingerID> touches;
    SDL_FingerID active_touch=0;
    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type==SDL_EVENT_QUIT) running=false;
        else if (event.type==SDL_EVENT_WILL_ENTER_BACKGROUND) { background=true; dragging=false; touches.clear(); touch_moved=true; audio.pause(true); renderer.reset_animation_clock(); }
        else if (event.type==SDL_EVENT_DID_ENTER_FOREGROUND) { background=false; audio.pause(false); renderer.reset_animation_clock(); }
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
      if (action&(1U<<6)) renderer.preview_animation();
      if (action&(1U<<7)) scene.toggle_navigation();
      if (action&(1U<<8)) scene.toggle_navigation(true);
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
