#include "jerusalem/script_story.hpp"
#include "jerusalem/x86.hpp"
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
using namespace jerusalem;
static int checks=0;
static void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("check failed: "+std::to_string(checks)); }
static void rejects(const std::function<void()>& f) {
  bool rejected=false; try { f(); } catch (const ParseError&) { rejected=true; } check(rejected);
}
static void put(std::vector<std::uint8_t>& b,std::size_t p,std::uint32_t n,int bytes=4) {
  for (int i=0;i<bytes;++i) b.at(p+i)=static_cast<std::uint8_t>(n>>(i*8));
}
// A one-section i386 PE: code at VA 0x401000 (file offset 0x200).
static std::vector<std::uint8_t> tiny_pe(const std::vector<std::uint8_t>& code) {
  std::vector<std::uint8_t> b(0x200+0x400,0);
  b[0]='M'; b[1]='Z'; put(b,0x3c,0x40);
  b[0x40]='P'; b[0x41]='E'; put(b,0x44,0x14c,2); put(b,0x46,1,2); put(b,0x54,0xe0,2);
  put(b,0x58,0x10b,2); put(b,0x58+28,0x400000); put(b,0x58+56,0x3000);
  const std::size_t s=0x58+0xe0;
  b[s]='.'; b[s+1]='t'; put(b,s+8,0x400); put(b,s+12,0x1000); put(b,s+16,0x400); put(b,s+20,0x200);
  std::copy(code.begin(),code.end(),b.begin()+0x200);
  return b;
}

static void machine_tests() {
  std::vector<std::uint8_t> code(0x400,0x90);
  const std::vector<std::uint8_t> f1{
    0x55,0x89,0xe5,0x8b,0x45,0x08,0x03,0x45,0x0c,0x50,0xe8,0xf1,0x00,0x00,0x00,  // a1+a2 -> trap(0x401100)
    0x83,0xf8,0x0a,0x7c,0x01,0x40,0x5d,0xc2,0x08,0x00};                          // >=10: +1
  std::copy(f1.begin(),f1.end(),code.begin());
  const std::vector<std::uint8_t> f2{0xa1,0x00,0x00,0x00,0x00,0xc3};  // mov eax,[0]
  std::copy(f2.begin(),f2.end(),code.begin()+0x20);
  const std::vector<std::uint8_t> f3{0xeb,0xfe};  // jmp $
  std::copy(f3.begin(),f3.end(),code.begin()+0x30);
  // f4(a): switch through a jump table, sar/imul/setcc/movsx and a global write.
  const std::vector<std::uint8_t> f4{
    0x8b,0x44,0x24,0x04,                    // mov eax,[esp+4]
    0x83,0xf8,0x02,0x77,0x21,               // cmp eax,2; ja default
    0xff,0x24,0x85,0x80,0x10,0x40,0x00,     // jmp [eax*4+0x401080]
    0xb8,0xf6,0xff,0xff,0xff,               // case0: mov eax,-10
    0xc1,0xf8,0x01,0xc3,                    // sar eax,1; ret
    0x6b,0xc0,0x07,0xc3,                    // case1: imul eax,eax,7; ret
    0x0f,0xbe,0x05,0x90,0x10,0x40,0x00,     // case2: movsx eax,byte [0x401090]
    0xa3,0x94,0x10,0x40,0x00,0xc3,          // mov [0x401094],eax; ret
    0x31,0xc0,0x83,0xf8,0x00,0x0f,0x94,0xc0,0xc3};  // default: eax=(0==0)
  std::copy(f4.begin(),f4.end(),code.begin()+0x40);
  put(code,0x80,0x401050); put(code,0x84,0x401059); put(code,0x88,0x40105d);
  code[0x90]=0xfb;
  X86Machine m{Bytes(tiny_pe(code))};
  std::vector<std::uint32_t> seen;
  m.trap(0x401100,[&](const X86Machine::Call& c){ seen.push_back(c.args.at(0)); return c.args[0]*2; },1);
  check(m.call(0x401000,0,{3,4})==15);
  check(m.call(0x401000,0,{1,2})==6);
  check(seen==std::vector<std::uint32_t>({7,3}));
  rejects([&]{ m.call(0x401020,0,{}); });
  rejects([&]{ m.call(0x401030,0,{},1000); });
  check(m.call(0x401040,0,{0})==std::uint32_t(-5));
  check(m.call(0x401040,0,{1})==7);
  check(m.call(0x401040,0,{2})==std::uint32_t(-5) && m.read32(0x401094)==std::uint32_t(-5));
  check(m.call(0x401040,0,{9})==1);
  // The image is restorable (script saves keep the changed globals).
  auto image=m.image(); image.at(0x1094)=0x11; m.restore_image(image); check(m.read8(0x401094)==0x11);
  rejects([]{ X86Machine bad{Bytes(std::vector<std::uint8_t>(0x800,0))}; });
}

static void topic_tests() {
  const auto book=parse_topic_book("#3299\t3299\r\n32002\tMillets 1\t1\n100002\tContinue\n\n#32008\t3299\n32010\tIshmael\t1\n#junk\n7\n");
  check(book.sections.size()==2);
  check(book.sections.at(3299).set==3299 && book.sections.at(3299).topics==std::vector<std::uint32_t>({32002,100002}));
  check(book.sections.at(32008).set==3299 && book.sections.at(32008).topics==std::vector<std::uint32_t>({32010}));
}

// With JERUSALEM_EXE (and optionally JERUSALEM_KEYWORDS) pointing at the user's files.
static void original_tests() {
  const char* path=std::getenv("JERUSALEM_EXE");
  if (!path) { std::cout<<"script: JERUSALEM_EXE not set; original-program checks skipped\n"; return; }
  std::ifstream in(path,std::ios::binary);
  std::vector<std::uint8_t> exe((std::istreambuf_iterator<char>(in)),{});
  // The interpreted A02 setup matches the pinned static extraction.
  const auto expected=extract_a02_stage(Bytes(exe));
  const auto actual=extract_area_model(Bytes(exe),2).stage;
  check(write_stage(expected)==write_stage(actual));
  const auto a03=extract_area_model(Bytes(exe),3);
  check(a03.puzzles.size()==154 && a03.graph.nodes.size()==29 && a03.stage.access.size()==165 && a03.sounds.size()==345);
  std::string ini;
  if (const char* k=std::getenv("JERUSALEM_KEYWORDS")) { std::ifstream f(k); ini.assign((std::istreambuf_iterator<char>(f)),{}); }
  ScriptStory story(exe,parse_topic_book(ini));
  story.start(3,0);
  std::vector<std::pair<StoryCue::Kind,std::uint32_t>> cues;
  const auto pump=[&]{ while (auto c=story.next()) { cues.push_back({c->kind,c->id}); if (c->kind==StoryCue::Speech||c->kind==StoryCue::Movie) story.complete(c->serial); } };
  pump();
  check(story.area()==3 && cues.size()==5 && cues[0]==std::pair{StoryCue::Puzzle,32104U} && cues[1]==std::pair{StoryCue::Speech,32000U} && cues[4]==std::pair{StoryCue::Puzzle,32109U});
  const auto hot=story.hotspots();
  check(hot.size()==1 && hot[0].object==39997);
  check(story.activate(39997,1));
  const auto bytes=story.save({},42);
  ScriptStory copy(exe,parse_topic_book(ini));
  copy.load(Bytes(bytes),42);
  check(copy.save({},42)==bytes && copy.topics()==story.topics() && copy.scene()==32109);
  rejects([&]{ copy.load(Bytes(bytes),43); });
  if (!ini.empty()) {
    check(story.topics()==std::vector<std::uint32_t>({32002,32004,32008,100002}));
    check(story.choose_topic(32002)); cues.clear(); pump();
    check(!cues.empty() && cues[0]==std::pair{StoryCue::Puzzle,32103U});
  }
}

int main() {
  try { machine_tests(); topic_tests(); original_tests(); }
  catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
  std::cout<<"script: "<<checks<<" checks passed\n";
}
