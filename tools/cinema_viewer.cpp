#include "jerusalem/cinema.hpp"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
int main(int argc,char** argv){try{
 if(argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--exit-at-end"))throw jerusalem::ParseError("Usage: jerusalem_cinema FILE [--exit-at-end]; Space pauses, Escape quits");
 jerusalem::CinemaDecoder movie(jerusalem::read_file(argv[1]));const auto& info=movie.info();
 if(!info.rate_word || info.rate_word>10000)throw jerusalem::ParseError("unsupported cinema frame rate");
 if(SDL_Init(SDL_INIT_VIDEO|(info.sounds.empty()?0:SDL_INIT_AUDIO))!=0)throw std::runtime_error(SDL_GetError());
 struct Quit{~Quit(){SDL_Quit();}} quit;
 struct Audio{SDL_AudioDeviceID id=0;~Audio(){if(id)SDL_CloseAudioDevice(id);}} audio;
 if(!info.sounds.empty()){SDL_AudioSpec wanted{};wanted.freq=static_cast<int>(info.sounds[0].rate);wanted.channels=static_cast<Uint8>(info.sounds[0].channels);wanted.format=AUDIO_S16LSB;wanted.samples=1024;audio.id=SDL_OpenAudioDevice(nullptr,0,&wanted,nullptr,0);if(!audio.id)throw std::runtime_error(SDL_GetError());}
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Jerusalem / native Cinema",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,672,SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
 if(!window)throw std::runtime_error(SDL_GetError());
 std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),-1,0),SDL_DestroyRenderer);
 if(!renderer)throw std::runtime_error(SDL_GetError());
 std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,static_cast<int>(info.width),static_cast<int>(info.height)),SDL_DestroyTexture);
 if(!texture)throw std::runtime_error(SDL_GetError());
 const auto start=SDL_GetTicks64();std::uint64_t pause_start=0,paused_total=0,frames=0;
 bool running=true,paused=false,user_paused=false,foreground=true,displayed=false,reported=false;
 std::cout << "CNM playback: " << info.width << 'x' << info.height << " fps=" << double(info.rate_word)/100 << " audio_tracks=" << info.sounds.size() << '\n';
 while(running){
  SDL_Event event;while(SDL_PollEvent(&event)){
   if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE))running=false;
   if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_SPACE)user_paused=!user_paused;
   if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST)foreground=false;
   if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)foreground=true;
  }
  const bool next_paused=user_paused || !foreground;const auto now=SDL_GetTicks64();
  if(next_paused!=paused){if(next_paused)pause_start=now;else paused_total+=now-pause_start;paused=next_paused;if(audio.id)SDL_PauseAudioDevice(audio.id,paused);}
  const auto elapsed=(paused?pause_start:now)-start-paused_total,desired=1+elapsed*info.rate_word/100000;
  unsigned budget=4;
  while(!paused && !movie.done() && frames<desired && budget){const auto decoded=movie.next();
   if(decoded.kind==jerusalem::CinemaEvent::Audio && decoded.channel==0){if(SDL_GetQueuedAudioSize(audio.id)>4*1024*1024 || SDL_QueueAudio(audio.id,decoded.pcm.data(),static_cast<Uint32>(decoded.pcm.size()))!=0)throw std::runtime_error("cinema PCM queue failed");SDL_PauseAudioDevice(audio.id,0);}
   if(decoded.kind==jerusalem::CinemaEvent::Frame || decoded.kind==jerusalem::CinemaEvent::Dropped){const auto image=movie.image();if(SDL_UpdateTexture(texture.get(),nullptr,image.rgba.data(),static_cast<int>(image.width*4))!=0)throw std::runtime_error(SDL_GetError());displayed=true;std::cout << "Cinema frame: " << frames++ << '\n';--budget;}
  }
  if(movie.done() && !reported){reported=true;std::cout << "Cinema decoded: " << frames << " frames\n";}
  int w,h;if(SDL_GetRendererOutputSize(renderer.get(),&w,&h)!=0)throw std::runtime_error(SDL_GetError());
  SDL_SetRenderDrawColor(renderer.get(),0,0,0,255);SDL_RenderClear(renderer.get());
  if(displayed && w>0 && h>0){const double scale=std::min(double(w)/info.width,double(h)/info.height);SDL_Rect target{int((w-info.width*scale)/2),int((h-info.height*scale)/2),int(info.width*scale),int(info.height*scale)};if(SDL_RenderCopy(renderer.get(),texture.get(),nullptr,&target)!=0)throw std::runtime_error(SDL_GetError());}
  SDL_RenderPresent(renderer.get());
  if(argc==3 && movie.done() && (!audio.id || SDL_GetQueuedAudioSize(audio.id)==0))running=false;
  SDL_Delay(4);
 }
 return 0;
}catch(const std::exception& e){std::cerr << "Cinema error: " << e.what() << '\n';return 1;}}
