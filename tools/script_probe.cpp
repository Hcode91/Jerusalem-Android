// Runs the original area story handlers through X86Machine with logging engine stubs.
// Usage: script_probe Jerusalem.ex_ <handler-hex> <nargs> <first> <last> [arg2 [arg3 ...]]
// Prints, for every case ID whose trace differs from the default path, the engine
// calls it makes, and every interpreter failure.
#include "jerusalem/engine_api.hpp"
#include "jerusalem/x86.hpp"
#include "jerusalem/script_area.hpp"
#include "jerusalem/script_story.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <functional>
#include <set>
#include <sstream>

using namespace jerusalem;

int main(int argc,char** argv) try {
  if (argc>=6 && std::string(argv[2])=="fuzz") {
    // script_probe exe fuzz keywords.ini runs steps: random play-throughs; reports failures and coverage.
    std::ifstream f(argv[1],std::ios::binary); std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)),{});
    std::ifstream k(argv[3]); std::string ini((std::istreambuf_iterator<char>(k)),{});
    const auto book=parse_topic_book(ini);
    std::map<std::string,unsigned> errors; std::set<std::uint32_t> scenes; std::set<unsigned> areas; std::set<std::string> movies;
    std::uint32_t seed=12345; const auto rnd=[&](std::uint32_t n){ seed=seed*1103515245U+12345U; return (seed>>8)%n; };
    for (int run=0;run<std::stoi(argv[4]);++run) {
      ScriptStory story(d,book,run+1);
      try {
        story.start(3,0);
        for (int step=0;step<std::stoi(argv[5]);++step) {
          while (auto c=story.next()) { if (c->kind==StoryCue::Movie) movies.insert(c->asset); if (c->kind==StoryCue::Movie||c->kind==StoryCue::Speech) story.complete(c->serial); }
          scenes.insert(story.scene()); areas.insert(story.area());
          if (story.boundary()) break;
          const auto topics=story.topics();
          if (!topics.empty() && rnd(4)) { story.choose_topic(topics[rnd(topics.size())]); continue; }
          std::vector<std::function<void()>> options;
          for (const auto& h:story.hotspots()) options.push_back([&story,h]{ story.activate(h.object,h.event); });
          for (const auto& e:story.graph().exits) if (e.source==story.scene()) options.push_back([&story,e]{ if (e.puzzle) story.enter_puzzle(e.target); else story.enter_node(e.target,e.arrival); });
          const auto bag=story.inventory();
          for (const auto item:bag) for (const auto& h:story.hotspots()) options.push_back([&story,h,item]{ story.activate(h.object,h.event,item); });
          for (const auto a:bag) for (const auto b:bag) if (a!=b) options.push_back([&story,a,b]{ story.combine(a,b); });
          options.push_back([&story]{ story.tick(5000); });
          options[rnd(options.size())]();
        }
      } catch (const std::exception& e) { ++errors[e.what()]; }
    }
    std::cout<<"areas:"; for (auto a:areas) std::cout<<" "<<a; std::cout<<"\n"; for (auto s:scenes) std::cout<<s<<" "; std::cout<<"\nscenes "<<scenes.size()<<" movies "<<movies.size()<<"\n";
    for (const auto& [e,n]:errors) std::cout<<"ERROR x"<<n<<": "<<e<<"\n";
    return 0;
  }
  if (argc>=4 && std::string(argv[2])=="play") {
    // script_probe exe play keywords.ini [actions...]: u<object>[:event][@item] use, t<topic>, n<node>, p<puzzle>,
    // x<index> puzzle exit, c<a>,<b> combine, w<ms> wait. Media completes instantly.
    std::ifstream f(argv[1],std::ios::binary); std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)),{});
    std::ifstream k(argv[3]); std::string ini((std::istreambuf_iterator<char>(k)),{});
    ScriptStory story(d,parse_topic_book(ini));
    const auto pump=[&]{
      while (true) {
        if (auto c=story.next()) {
          const char* kinds[]{"Node","Puzzle","Movie","Speech","Boundary"};
          std::cout<<"  cue "<<kinds[c->kind]<<" "<<c->id<<" "<<c->asset<<"\n";
          if (c->kind==StoryCue::Movie || c->kind==StoryCue::Speech) story.complete(c->serial);
        } else break;
      }
    };
    const auto status=[&]{
      std::cout<<"area "<<story.area()<<" scene "<<story.scene()<<(story.puzzle()?" puzzle":" node")<<" bag [";
      for (auto i:story.inventory()) std::cout<<i<<" ";
      std::cout<<"] topics [";
      for (auto t:story.topics()) std::cout<<t<<" ";
      std::cout<<"]\n  hotspots:";
      for (const auto& h:story.hotspots()) std::cout<<" "<<(h.object>=ScriptStory::exit_tag?"x"+std::to_string(h.object&0xffffff):std::to_string(h.object))<<":"<<h.event;
      std::cout<<"\n  exits:";
      for (const auto& e:story.graph().exits) if (e.source==story.scene()) std::cout<<" "<<(e.puzzle?"p":"n")<<e.target;
      std::cout<<"\n";
    };
    try {
      story.start(3,0); pump(); status();
      for (int i=4;i<argc;++i) {
        std::string a=argv[i]; std::cout<<"> "<<a<<"\n";
        const auto num=[&](std::size_t from){ return static_cast<std::uint32_t>(std::stoul(a.substr(from))); };
        if (a[0]=='u') { auto rest=a.substr(1); std::optional<std::uint32_t> item; std::uint32_t ev=0;
          if (auto at=rest.find('@');at!=std::string::npos) { item=std::stoul(rest.substr(at+1)); rest.resize(at); }
          if (auto co=rest.find(':');co!=std::string::npos) { ev=std::stoul(rest.substr(co+1)); rest.resize(co); }
          story.activate(std::stoul(rest),ev,item); }
        else if (a[0]=='t' && a!="trail") story.choose_topic(num(1));
        else if (a[0]=='n') story.enter_node(num(1));
        else if (a[0]=='p') story.enter_puzzle(num(1));
        else if (a[0]=='x') story.activate(ScriptStory::exit_tag|num(1),0);
        else if (a[0]=='c') { const auto comma=a.find(','); std::cout<<"  combined -> "<<story.combine(std::stoul(a.substr(1,comma-1)),std::stoul(a.substr(comma+1))).value_or(0)<<"\n"; }
        else if (a[0]=='w') story.tick(num(1));
        else if (a=="trail") { for (const auto& t:story.trail()) std::cout<<" "<<t; std::cout<<"\n"; }
        else if (a=="save") { const auto bytes=story.save({},7); story.load(Bytes(bytes),7); std::cout<<"  save/load "<<bytes.size()<<" bytes\n"; }
        pump(); status();
      }
    } catch (const std::exception& e) {
      std::cout<<"ERROR "<<e.what()<<"\n  trail:"; for (const auto& t:story.trail()) std::cout<<" "<<t; std::cout<<"\n";
    }
    return 0;
  }
  if (argc==4 && std::string(argv[2])=="area") {
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)),{});
    const auto a=extract_area_model(Bytes(d),std::stoul(argv[3]));
    std::cout<<"objects "<<a.stage.objects.size()<<" backgrounds "<<a.stage.backgrounds.size()<<" puzzles "<<a.puzzles.size()<<" access "<<a.stage.access.size()
      <<" layers "<<a.stage.layers.size()<<" dialogues "<<a.stage.dialogues.size()<<" sounds "<<a.sounds.size()<<" puzzle-ambient "<<a.stage.ambient.size()
      <<" nodes "<<a.graph.nodes.size()<<" exits "<<a.graph.exits.size()<<" puzzle-exits "<<a.puzzle_exits.size()<<" rot-ambient "<<a.graph.ambient.size()
      <<" vars "<<a.variables.size()<<" anim-events "<<a.animation_events.size()<<"\n";
    for (const auto& [k,v]:a.calls) std::cout<<"  "<<k<<" "<<v<<"\n";
    for (const auto& e:a.animation_events) std::cout<<"  aniide "<<e.object<<" "<<e.presentation<<" "<<e.scene<<" "<<e.event<<" "<<e.panorama<<"\n";
    for (const auto& l:a.stage.layers) if (l.animated) std::cout<<"  ani "<<l.object<<"/"<<l.presentation<<" scene "<<l.scene<<" "<<l.image<<" ch "<<l.channel<<" frames "<<l.frames<<" mode "<<l.mode<<" fps "<<l.fps<<"\n";
    for (const auto& n:a.graph.nodes) std::cout<<"  node "<<n.id<<" "<<n.asset<<" "<<n.channels<<"\n";
    return 0;
  }
  if (argc<6) { std::cerr<<"usage: script_probe exe handler nargs first last [args...]\n"; return 2; }
  std::ifstream in(argv[1],std::ios::binary);
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),{});
  X86Machine m{Bytes(data)};
  const auto handler=std::stoul(argv[2],nullptr,16);
  const unsigned nargs=std::stoul(argv[3]);
  const std::uint32_t first=std::stoul(argv[4]),last=std::stoul(argv[5]);
  std::vector<std::uint32_t> extra;
  for (int i=6;i<argc;++i) extra.push_back(std::stoul(argv[i],nullptr,0));
  const auto app=m.scratch(0x1000);
  m.write32(0x4d0a90,app);
  std::ostringstream trace;
  std::map<std::uint32_t,std::uint32_t> vars;
  for (const auto& api:engine_apis) {
    m.trap(api.address,[&,api](const X86Machine::Call& c)->std::uint32_t {
      trace<<api.name<<'(';
      for (std::size_t i=0;i<c.args.size();++i) trace<<(i?",":"")<<std::int32_t(c.args[i]);
      trace<<") ";
      if (const auto v=variable_api(api.address)) { if (v->kind==2) return vars[c.args[0]]; vars[c.args[0]]=c.args[1]; return 1; }
      if (api.name=="GetSpace") return 3;
      if (api.name=="IsSpeaking"||api.name=="InRotation"||api.name=="BagIsIn") return 0;
      if (api.name=="rand") return 0;
      return 1;
    },api.args,api.callee_pops);
  }
  std::map<std::string,std::vector<std::uint32_t>> groups;
  std::map<std::string,std::vector<std::uint32_t>> failures;
  for (std::uint32_t id=first;id<=last;++id) {
    trace.str(""); vars.clear(); m.reset_scratch(); m.scratch(0x1000);
    std::vector<std::uint32_t> args{id};
    args.insert(args.end(),extra.begin(),extra.end());
    args.resize(nargs,0);
    try { m.call(handler,app,args,200000); groups[trace.str()].push_back(id); }
    catch (const std::exception& e) { failures[e.what()].push_back(id); }
  }
  std::size_t largest=0; std::string dflt;
  for (const auto& [t,ids]:groups) if (ids.size()>largest) { largest=ids.size(); dflt=t; }
  std::cout<<"default ("<<largest<<" ids): "<<dflt<<"\n";
  for (const auto& [t,ids]:groups) {
    if (t==dflt) continue;
    std::cout<<ids.front();
    if (ids.size()>1) std::cout<<" (+"<<ids.size()-1<<": "<<ids.back()<<")";
    std::cout<<": "<<t<<"\n";
  }
  for (const auto& [e,ids]:failures) std::cout<<"FAIL "<<ids.size()<<" ids, first "<<ids.front()<<": "<<e<<"\n";
} catch (const std::exception& e) { std::cerr<<"error: "<<e.what()<<"\n"; return 1; }
