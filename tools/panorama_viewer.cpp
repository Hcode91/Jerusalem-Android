#include "jerusalem/panorama.hpp"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>

int main(int argc,char** argv) {
  try {
    if (argc!=2 && argc!=4) throw jerusalem::ParseError("Usage: jerusalem_viewer NODE.AQC [--frames COUNT]");
    std::optional<unsigned long> limit;
    if (argc==4) {
      if (std::string(argv[2])!="--frames") throw jerusalem::ParseError("unknown viewer option");
      limit=std::stoul(argv[3]);
      if (*limit==0) throw jerusalem::ParseError("frame limit must be positive");
    }
    const auto data=jerusalem::read_file(argv[1]);
    const auto geometry=jerusalem::panorama_geometry(jerusalem::Bytes(data));
    const auto source=jerusalem::decode_aqc(jerusalem::Bytes(data));
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
    bool running=true,dirty=true,dragging=false;
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
        } else if (event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT) dragging=true;
        else if (event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT) dragging=false;
        else if (event.type==SDL_MOUSEMOTION && dragging) {
          camera.yaw-=event.motion.xrel*camera.fov/std::max(width,1);
          camera.pitch-=event.motion.yrel*camera.fov/std::max(height,1); dirty=true;
        } else if (event.type==SDL_MOUSEWHEEL) {
          camera.fov=std::clamp(camera.fov-(event.wheel.y*5),30.0,87.0); dirty=true;
        }
      }
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
