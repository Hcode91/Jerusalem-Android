#include "jerusalem/panorama.hpp"
#include "jerusalem/aqc_animation.hpp"
#include <SDL.h>
#include <utility>
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>

int main(int argc,char** argv) {
  try {
    if (argc<2) throw jerusalem::ParseError("Usage: jerusalem_viewer NODE.AQC [--frames COUNT] [--preview]");
    std::optional<unsigned long> limit;
    bool start_preview=false;
    for (int i=2;i<argc;++i) {
      if (std::string(argv[i])=="--preview") start_preview=true;
      else if (std::string(argv[i])=="--frames" && i+1<argc) {
        limit=std::stoul(argv[++i]);
        if (*limit==0) throw jerusalem::ParseError("frame limit must be positive");
      } else throw jerusalem::ParseError("unknown viewer option");
    }
    auto data=jerusalem::read_file(argv[1]);
    const auto geometry=jerusalem::panorama_geometry(jerusalem::Bytes(data));
    auto source=jerusalem::decode_aqc(jerusalem::Bytes(data));
    std::unique_ptr<jerusalem::AqcPreview> preview;
    try { preview=std::make_unique<jerusalem::AqcPreview>(std::move(data),source); }
    catch (const jerusalem::ParseError& e) { std::cerr << "Animation unavailable: " << e.what() << '\n'; if (start_preview) throw; }
    if (SDL_Init(SDL_INIT_VIDEO)!=0) throw std::runtime_error(SDL_GetError());
    struct Quit { ~Quit(){SDL_Quit();} } quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Jerusalem / SDL2 panorama viewer",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                         960,672,SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
    if (!window) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(),-1,0),SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture(nullptr,SDL_DestroyTexture);
    jerusalem::Camera camera;
    std::optional<jerusalem::Camera> saved_camera;
    bool running=true,dirty=true,dragging=false;
    bool foreground=true;
    Uint32 preview_tick=SDL_GetTicks();
    const auto cycle_preview=[&] {
      if (!preview) { std::cerr << "Animation preview is unavailable for this node\n"; return; }
      const bool was_active=preview->channel().has_value();
      if (preview->next()) {
        if (!was_active) saved_camera=camera;
        const auto& rect=preview->selected()->frames.front();
        camera.yaw=(rect.left+rect.width/2.0)/geometry.width*360-180;
        camera.pitch=(rect.top+rect.height/2.0)/geometry.height*(geometry.pitch_max-geometry.pitch_min)+geometry.pitch_min;
        camera.fov=30;
        std::cout << "Animation preview: channel " << *preview->channel()+1 << " at 12 fps inspector speed; game triggers not implemented\n";
      } else if (saved_camera) { camera=*saved_camera; saved_camera.reset(); }
      preview_tick=SDL_GetTicks(); dirty=true;
    };
    if (start_preview) cycle_preview();
    unsigned long frames=0;
    int texture_width=0,texture_height=0;
    while (running) {
      int width,height;
      if (SDL_GetRendererOutputSize(renderer.get(),&width,&height)!=0) throw std::runtime_error(SDL_GetError());
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type==SDL_QUIT) running=false;
        else if (event.type==SDL_KEYDOWN) {
          if (event.key.keysym.sym==SDLK_ESCAPE) running=false;
          if (event.key.keysym.sym==SDLK_HOME) { camera={}; dirty=true; }
          if (event.key.keysym.sym==SDLK_a) cycle_preview();
        } else if (event.type==SDL_WINDOWEVENT && (event.window.event==SDL_WINDOWEVENT_FOCUS_LOST || event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)) {
          foreground=event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED;
          preview_tick=SDL_GetTicks();
        } else if (event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT) dragging=true;
        else if (event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT) dragging=false;
        else if (event.type==SDL_MOUSEMOTION && dragging) {
          camera.yaw-=event.motion.xrel*camera.fov/std::max(width,1);
          camera.pitch-=event.motion.yrel*camera.fov/std::max(height,1); dirty=true;
        } else if (event.type==SDL_MOUSEWHEEL) {
          camera.fov=std::clamp(camera.fov-(event.wheel.y*5),30.0,87.0); dirty=true;
        }
      }
      const auto now=SDL_GetTicks();
      if (preview && foreground && preview->update(source,now-preview_tick)) {
        dirty=true;
        if (preview->channel()) std::cout << "AQC preview frame: channel=" << *preview->channel() << " frame=" << *preview->frame() << '\n';
      }
      preview_tick=now;
      if (width>0 && height>0) {
        const double scale=std::min(1.0,640.0/std::max(width,height));
        const auto rw=std::max(1,static_cast<int>(width*scale)),rh=std::max(1,static_cast<int>(height*scale));
        if (rw!=texture_width || rh!=texture_height) {
          texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,rw,rh));
          if (!texture) throw std::runtime_error(SDL_GetError());
          texture_width=rw; texture_height=rh; dirty=true;
        }
        if (dirty) {
          camera=jerusalem::constrain_camera(geometry,camera,rw,rh);
          const auto image=jerusalem::render_panorama(source,geometry,camera,rw,rh);
          if (SDL_UpdateTexture(texture.get(),nullptr,image.rgba.data(),rw*4)!=0) throw std::runtime_error(SDL_GetError());
          dirty=false;
        }
        if (SDL_RenderClear(renderer.get())!=0 || SDL_RenderCopy(renderer.get(),texture.get(),nullptr,nullptr)!=0)
          throw std::runtime_error(SDL_GetError());
        SDL_RenderPresent(renderer.get());
        if (limit && ++frames>=*limit) running=false;
      }
      SDL_Delay(16);
    }
    std::cout << "SDL2 panorama viewer completed; no navigation or scripted interactions implemented\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
