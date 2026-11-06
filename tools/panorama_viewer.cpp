#include "jerusalem/panorama.hpp"
#include "jerusalem/aqc_animation.hpp"
#include "jerusalem/navigation.hpp"
#include "jerusalem/interactions.hpp"
#include "jerusalem/checkpoint.hpp"
#include <SDL.h>
#include <utility>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>

int main(int argc,char** argv) {
  try {
    if (argc<2) throw jerusalem::ParseError("Usage: jerusalem_viewer NODE.AQC [--frames COUNT] [--preview] [--navigation MODEL] [--checkpoint FILE] [--load-checkpoint] [--save-checkpoint]");
    std::optional<unsigned long> limit;
    bool start_preview=false;
    std::optional<std::filesystem::path> navigation_file,checkpoint_file;
    bool load_start=false,save_start=false;
    for (int i=2;i<argc;++i) {
      if (std::string(argv[i])=="--preview") start_preview=true;
      else if (std::string(argv[i])=="--navigation" && i+1<argc) navigation_file=argv[++i];
      else if (std::string(argv[i])=="--checkpoint" && i+1<argc) checkpoint_file=argv[++i];
      else if (std::string(argv[i])=="--load-checkpoint") load_start=true;
      else if (std::string(argv[i])=="--save-checkpoint") save_start=true;
      else if (std::string(argv[i])=="--frames" && i+1<argc) {
        limit=std::stoul(argv[++i]);
        if (*limit==0) throw jerusalem::ParseError("frame limit must be positive");
      } else throw jerusalem::ParseError("unknown viewer option");
    }
    if((checkpoint_file && !navigation_file) || ((load_start || save_start) && !checkpoint_file)) throw jerusalem::ParseError("checkpoint controls require --navigation MODEL and --checkpoint FILE");
    auto data=jerusalem::read_file(argv[1]);
    auto geometry=jerusalem::panorama_geometry(jerusalem::Bytes(data));
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
    unsigned long frames=0;
    int texture_width=0,texture_height=0;
    std::optional<jerusalem::SceneGraph> navigation;
    std::unique_ptr<jerusalem::InteractionState> interactions;
    std::map<std::uint32_t,std::filesystem::path> scene_assets;
    std::uint32_t current=0;
    if (navigation_file) {
      const auto bytes=jerusalem::read_file(*navigation_file);
      navigation=jerusalem::parse_navigation(std::string(bytes.begin(),bytes.end()));
      scene_assets=jerusalem::resolve_scene_assets(navigation_file->parent_path().empty()?std::filesystem::path("."):navigation_file->parent_path(),*navigation);
      auto stem=std::filesystem::path(argv[1]).stem().string();
      for (auto& c:stem) if (c>='a'&&c<='z') c=char(c-'a'+'A');
      for (const auto& node:navigation->nodes) if (node.asset==stem) current=node.id;
      if (!current) throw jerusalem::ParseError("initial AQC is not in the supplied navigation model");
      interactions=std::make_unique<jerusalem::InteractionState>(*navigation);
      if(!navigation->items.empty()) {
        const auto input=jerusalem::read_file(argv[1]);
        source=jerusalem::compose_items(jerusalem::AqcAnimation(input),source,interactions->visibility(current));
        preview=std::make_unique<jerusalem::AqcPreview>(input,source);
        std::cout << "Pickup preview ready; cyan markers collect supported items; I lists inventory; --checkpoint FILE enables F5/F9 saves\n";
      }
      std::cout << "A01 traversal preview ready; story and puzzle actions are not implemented\n";
    }
    const auto enter=[&](std::uint32_t target,jerusalem::Camera arrival,std::unique_ptr<jerusalem::InteractionState> next=nullptr) {
      auto bytes=jerusalem::read_file(scene_assets.at(target));
      auto image=jerusalem::decode_aqc(jerusalem::Bytes(bytes));
      const auto next_geometry=jerusalem::panorama_geometry(jerusalem::Bytes(bytes));
      if(jerusalem::parse_aqc_channels(jerusalem::Bytes(bytes)).size()!=jerusalem::scene_node(*navigation,target).channels)
        throw jerusalem::ParseError("scene channel count differs from original setup");
      if(!navigation->items.empty()) image=jerusalem::compose_items(jerusalem::AqcAnimation(bytes),image,(next?next:interactions)->visibility(target));
      auto next_preview=std::make_unique<jerusalem::AqcPreview>(std::move(bytes),image);
      source=std::move(image);geometry=next_geometry;preview=std::move(next_preview);
      if(next)interactions=std::move(next);
      current=target;camera=arrival;saved_camera.reset();preview_tick=SDL_GetTicks();dirty=true;
      const auto title=jerusalem::scene_node(*navigation,current).asset+" / pickup preview; I inventory, F5 save, F9 load";
      SDL_SetWindowTitle(window.get(),title.c_str());
    };
    const auto save_checkpoint=[&] {
      if(!checkpoint_file || !navigation || !interactions) throw jerusalem::ParseError("use --navigation MODEL --checkpoint FILE for saving");
      const auto view=jerusalem::constrain_camera(geometry,camera,texture_width>0?texture_width:640,texture_height>0?texture_height:448);
      const jerusalem::Checkpoint cp{current,view,interactions->inventory()};
      jerusalem::write_checkpoint(*checkpoint_file,*navigation,jerusalem::checkpoint_assets(*navigation,scene_assets),cp);
      std::cout << "Checkpoint saved: node=" << current << " inventory=" << cp.inventory.size() << '\n';
    };
    const auto load_checkpoint=[&] {
      if(!checkpoint_file || !navigation) throw jerusalem::ParseError("use --navigation MODEL --checkpoint FILE for loading");
      const auto cp=jerusalem::read_checkpoint(*checkpoint_file,*navigation,jerusalem::checkpoint_assets(*navigation,scene_assets));
      auto next=std::make_unique<jerusalem::InteractionState>(jerusalem::restore_interactions(*navigation,cp));
      enter(cp.node,cp.camera,std::move(next));
      std::cout << "Checkpoint loaded: node=" << current << " inventory=" << cp.inventory.size() << '\n';
    };
    if(load_start)load_checkpoint();
    if(save_start)save_checkpoint();
    if(start_preview)cycle_preview();
    bool mouse_moved=false; int mouse_start_x=0,mouse_start_y=0;
    const auto tap=[&](float x,float y,int width,int height) {
      if (!navigation || width<=0 || height<=0) return;
      camera=jerusalem::constrain_camera(geometry,camera,width,height);
      const auto point=jerusalem::hotspot_point(geometry,jerusalem::panorama_point(geometry,camera,2*x/width-1,2*y/height-1,width,height));
      std::unique_ptr<jerusalem::InteractionState> next;
      const auto item=interactions->hit(current,point.x,point.y);
      auto target=current;auto arrival=camera;
      if(item) {next=std::make_unique<jerusalem::InteractionState>(*interactions);next->pickup(*item);}
      else {
        const auto match=jerusalem::navigation_hit(*navigation,current,point.x,point.y);
        if(!match)return;
        const auto& exit=navigation->exits[*match];
        if(exit.puzzle){std::cout << "Original puzzle action is not implemented; no route bypassed\n";return;}
        target=exit.target;arrival=exit.arrival;
      }
      try {
        enter(target,arrival,std::move(next));
        if(item)std::cout << "Collected: " << jerusalem::collectible(*navigation,*item).name << " [" << *item << "]\n";
        else std::cout << "A01 traversal: " << jerusalem::scene_node(*navigation,current).asset << '\n';
      }catch(const std::exception& e){std::cerr << "Cannot enter scene: " << e.what() << '\n';}

    };
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
          if(event.key.keysym.sym==SDLK_F5 || event.key.keysym.sym==SDLK_F9) {
            try {if(event.key.keysym.sym==SDLK_F5)save_checkpoint();else load_checkpoint();}
            catch(const std::exception& e){std::cerr << "Checkpoint error: " << e.what() << '\n';}
          }
          if(event.key.keysym.sym==SDLK_i && interactions) {
            std::cout << "Collected items: " << interactions->inventory().size() << '\n';
            for(const auto id:interactions->inventory()) std::cout << id << ' ' << jerusalem::collectible(*navigation,id).name << '\n';
            std::cout << "Item use/combinations are not implemented; checkpoint controls preserve this preview only\n";
          }
        } else if (event.type==SDL_WINDOWEVENT && (event.window.event==SDL_WINDOWEVENT_FOCUS_LOST || event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)) {
          foreground=event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED;
          preview_tick=SDL_GetTicks();
        } else if (event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT) { dragging=true; mouse_moved=false; mouse_start_x=event.button.x; mouse_start_y=event.button.y; }
        else if (event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT) {
          if (dragging && !mouse_moved) tap(float(event.button.x),float(event.button.y),width,height);
          dragging=false;
        }
        else if (event.type==SDL_MOUSEMOTION && dragging) {
          if (std::abs(event.motion.x-mouse_start_x)+std::abs(event.motion.y-mouse_start_y)>3) mouse_moved=true;
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
        if (navigation) for (const auto& exit:navigation->exits) if (exit.source==current && exit.enabled) {
          const auto point=jerusalem::hotspot_screen(geometry,camera,(exit.left+exit.right)/2.0,(exit.top+exit.bottom)/2.0,rw,rh);
          if (!point) continue;
          SDL_SetRenderDrawColor(renderer.get(),exit.puzzle?240:90,exit.puzzle?160:230,100,255);
          SDL_Rect marker{int((point->x+1)*width/2)-14,int((point->y+1)*height/2)-14,28,28};
          SDL_RenderDrawRect(renderer.get(),&marker);
        }
        if(navigation && interactions) for(const auto& item:navigation->items) if(item.node==current && !interactions->collected(item.id)) {
          const auto point=jerusalem::hotspot_screen(geometry,camera,(item.left+item.right)/2.0,(item.top+item.bottom)/2.0,rw,rh);
          if(!point)continue;
          SDL_SetRenderDrawColor(renderer.get(),90,200,255,255);
          SDL_Rect marker{int((point->x+1)*width/2)-14,int((point->y+1)*height/2)-14,28,28};SDL_RenderDrawRect(renderer.get(),&marker);
        }
        SDL_RenderPresent(renderer.get());
        if (limit && ++frames>=*limit) running=false;
      }
      SDL_Delay(16);
    }
    std::cout << "SDL2 panorama viewer completed; traversal preview is separate from unimplemented story/puzzle logic\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
