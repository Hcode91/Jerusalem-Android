#include "jerusalem/navigation.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace jerusalem {
namespace {
constexpr std::uint32_t begin=0x47cea0,end=0x47d8de;
class Pe {
  Bytes b_;
  struct Section { std::uint32_t rva,virtual_size,size,offset; };
  std::vector<Section> sections_;
 public:
  explicit Pe(Bytes b):b_(b) {
    if (!b.is(0,"MZ")) throw ParseError("not a PE executable");
    const auto pe=std::size_t(b.u32(0x3c));
    b.require(pe,24);
    if (!b.is(pe,std::string("PE\0\0",4)) || b.u16(pe+4)!=0x14c) throw ParseError("require x86 PE32");
    const auto count=b.u16(pe+6), optional=b.u16(pe+20);
    if (!count || count>96 || optional<96) throw ParseError("invalid PE section table");
    b.require(pe+24,optional);
    if (b.u16(pe+24)!=0x10b || b.u32(pe+52)!=0x400000) throw ParseError("unsupported PE image base");
    const auto table=pe+24+optional;
    for (unsigned i=0;i<count;++i) {
      const auto p=table+i*40; b.require(p,40);
      Section s{b.u32(p+12),b.u32(p+8),b.u32(p+16),b.u32(p+20)};
      b.require(s.offset,s.size); sections_.push_back(s);
    }
  }
  Bytes at(std::uint32_t address,std::size_t size) const {
    if (address<0x400000) throw ParseError("PE address outside image");
    const auto rva=address-0x400000;
    for (const auto& s:sections_) if (rva>=s.rva && rva-s.rva<s.size) {
      const auto delta=rva-s.rva;
      if (size>s.size-delta) throw ParseError("PE address spans section bounds");
      return b_.slice(std::size_t(s.offset)+delta,size);
    }
    throw ParseError("PE address is not file backed");
  }
  std::string name(std::uint32_t address) const {
    std::string text;
    for (unsigned i=0;i<64;++i) {
      const auto c=at(address+i,1).u8(0);
      if (!c) return text;
      if (!((c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_')) throw ParseError("unsafe scene asset name");
      text+=char(c);
    }
    throw ParseError("unterminated scene asset name");
  }
};
double real(std::uint32_t bits) {
  float value; std::memcpy(&value,&bits,4);
  if (!std::isfinite(value)) throw ParseError("invalid scene camera float");
  return value;
}
std::int32_t signed_word(std::uint32_t value) {
  return value<=0x7fffffffU ? static_cast<std::int32_t>(value) :
      static_cast<std::int32_t>(static_cast<std::int64_t>(value)-0x100000000LL);
}
}

std::vector<SetupCall> parse_setup_calls(Bytes b,std::uint32_t address) {
  if (b.size()>65536 || std::uint64_t(address)+b.size()>0x100000000ULL ||
      !b.is(0,std::string("\x56\x8b\xf1",3))) throw ParseError("unsupported setup prologue");
  std::size_t p=3;
  std::vector<std::uint32_t> stack;
  std::vector<SetupCall> calls;
  while (p<b.size()) {
    const auto op=b.u8(p);
    if (op==0x6a) {
      const auto v=b.u8(p+1);
      stack.push_back(v<128 ? v : 0xffffff00U|v); p+=2;
    } else if (op==0x68) { stack.push_back(b.u32(p+1)); p+=5; }
    else if (b.is(p,std::string("\x8b\xce",2))) {
      p+=2; if (b.u8(p)!=0xe8) throw ParseError("unexpected instruction after setup receiver");
    } else if (op==0xe8) {
      const auto target=std::int64_t(address)+static_cast<std::int64_t>(p)+5+b.i32(p+1);
      if (target<0 || target>0xffffffffLL || calls.size()>=256 || stack.empty()) throw ParseError("invalid setup call");
      std::reverse(stack.begin(),stack.end());
      calls.push_back({address+static_cast<std::uint32_t>(p),static_cast<std::uint32_t>(target),std::move(stack)});
      stack.clear(); p+=5;
    } else throw ParseError("unsupported setup instruction at byte "+std::to_string(p));
    if (stack.size()>16) throw ParseError("too many setup arguments");
  }
  if (!stack.empty()) throw ParseError("unfinished setup argument packet");
  return calls;
}

SceneGraph extract_a01_navigation(Bytes b) {
  if (b.size()!=811008) throw ParseError("unsupported Jerusalem executable size/version");
  std::uint64_t fingerprint=14695981039346656037ULL;
  for (std::size_t i=0;i<b.size();++i) fingerprint=(fingerprint^b.u8(i))*1099511628211ULL;
  if (fingerprint!=0x64b12e05ac088220ULL) throw ParseError("unsupported Jerusalem executable fingerprint");
  const Pe pe(b);
  const auto calls=parse_setup_calls(pe.at(begin,end-begin),begin);
  SceneGraph g;
  std::set<std::pair<std::uint32_t,std::uint32_t>> configured;
  for (const auto& call:calls) {
    const auto& a=call.arguments;
    if (call.target==0x42b530) {
      if (a.size()!=4 || a[2]!=0) throw ParseError("unsupported node registration");
      g.nodes.push_back({a[0],pe.name(a[1]),a[3]});
    } else if (call.target==0x42b860 || call.target==0x42bab0) {
      if (a.size()!=10 || a[2]!=0x4d0518 || a[7]>1) throw ParseError("unsupported exit registration");
      const auto slot=std::count_if(g.exits.begin(),g.exits.end(),[&](const Exit& e){return e.source==a[0];});
      g.exits.push_back({a[0],static_cast<std::uint32_t>(slot),a[1],call.target==0x42bab0,a[7]!=0,
                        signed_word(a[3]),signed_word(a[4]),signed_word(a[5]),signed_word(a[6]),a[8],a[9],{}});
    } else if (call.target==0x42b9b0 || call.target==0x42bc00) {
      if (a.size()!=(call.target==0x42b9b0 ? 10U : 7U)) throw ParseError("unsupported exit camera packet");
      const auto found=std::find_if(g.exits.begin(),g.exits.end(),[&](const Exit& e){return e.source==a[0] && e.slot==a[1];});
      if (found==g.exits.end() || !configured.insert({a[0],a[1]}).second) throw ParseError("exit camera has no unique owner");
      if (call.target==0x42b9b0) {
        if (found->puzzle || a[5]!=0 || a[6]!=1 || a[2]!=a[7] || a[3]!=a[8] || a[4]!=a[9])
          throw ParseError("unsupported transition camera semantics");
        found->arrival={real(a[7]),real(a[8]),real(a[9])};
      } else {
        if (!found->puzzle || a[5]!=0 || a[6]!=2) throw ParseError("unsupported puzzle camera semantics");
        found->arrival={real(a[2]),real(a[3]),real(a[4])};
      }
    }
  }
  if (g.nodes.size()!=6 || g.exits.size()!=17 || configured.size()!=17 ||
      std::count_if(g.exits.begin(),g.exits.end(),[](const Exit& e){return e.puzzle;})!=1)
    throw ParseError("incomplete supported A01 scene setup");
  validate_navigation(g);
  return g;
}

const SceneNode& scene_node(const SceneGraph& g,std::uint32_t id) {
  const auto node=std::find_if(g.nodes.begin(),g.nodes.end(),[&](const SceneNode& n){return n.id==id;});
  if (node==g.nodes.end()) throw ParseError("scene ID does not exist");
  return *node;
}
void validate_navigation(const SceneGraph& g) {
  if (g.nodes.empty() || g.nodes.size()>64 || g.exits.size()>512) throw ParseError("invalid navigation record counts");
  std::set<std::uint32_t> ids;
  std::set<std::string> names;
  std::set<std::pair<std::uint32_t,std::uint32_t>> slots;
  for (const auto& n:g.nodes) {
    if (!n.id || n.id>0x7fffffffU || !ids.insert(n.id).second || !names.insert(n.asset).second || n.asset.empty() || n.asset.size()>63 || n.channels>256)
      throw ParseError("invalid/duplicate scene node");
    for (unsigned char c:n.asset) if (!((c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_'))
      throw ParseError("unsafe scene asset name");
  }
  for (const auto& e:g.exits) {
    scene_node(g,e.source); if (!e.puzzle) scene_node(g,e.target);
    if (!e.target || e.target>0x7fffffffU || e.cursor>0x7fffffffU || e.event>0x7fffffffU ||
        e.slot>=64 || !slots.insert({e.source,e.slot}).second || e.left<0 || e.left>=e.right || e.right>3600 ||
        e.top< -900 || e.top>=e.bottom || e.bottom>900 ||
        !std::isfinite(e.arrival.yaw) || e.arrival.yaw<0 || e.arrival.yaw>360 ||
        !std::isfinite(e.arrival.pitch) || std::abs(e.arrival.pitch)>90 ||
        !std::isfinite(e.arrival.fov) || e.arrival.fov<30 || e.arrival.fov>87)
      throw ParseError("invalid navigation exit");
  }
}

std::string write_navigation(const SceneGraph& g) {
  validate_navigation(g);
  std::ostringstream out; out.imbue(std::locale::classic()); out << "JERUSALEM_A01_NAV_V1\n" << std::setprecision(12);
  for (const auto& n:g.nodes) out << "node " << n.id << ' ' << n.asset << ' ' << n.channels << '\n';
  for (const auto& e:g.exits)
    out << "exit " << e.source << ' ' << e.slot << ' ' << e.target << ' ' << e.puzzle << ' ' << e.enabled << ' '
        << e.left << ' ' << e.top << ' ' << e.right << ' ' << e.bottom << ' ' << e.cursor << ' ' << e.event << ' '
        << e.arrival.yaw << ' ' << e.arrival.pitch << ' ' << e.arrival.fov << '\n';
  return out.str();
}
SceneGraph parse_navigation(const std::string& text) {
  if (text.size()>128*1024) throw ParseError("navigation model exceeds 128 KiB limit");
  std::istringstream lines(text); std::string line;
  if (!std::getline(lines,line) || line!="JERUSALEM_A01_NAV_V1") throw ParseError("not a native A01 navigation model");
  SceneGraph g;
  while (std::getline(lines,line)) {
    if (line.empty()) continue;
    std::istringstream row(line); row.imbue(std::locale::classic()); std::string kind,extra; row>>kind;
    if (kind=="node") {
      SceneNode n{};
      if (!(row>>n.id>>n.asset>>n.channels) || (row>>extra) || g.nodes.size()>=64) throw ParseError("invalid node model row");
      g.nodes.push_back(std::move(n));
    } else if (kind=="exit") {
      Exit e{}; unsigned puzzle,enabled;
      if (!(row>>e.source>>e.slot>>e.target>>puzzle>>enabled>>e.left>>e.top>>e.right>>e.bottom>>e.cursor>>e.event
               >>e.arrival.yaw>>e.arrival.pitch>>e.arrival.fov) || puzzle>1 || enabled>1 || (row>>extra) || g.exits.size()>=512)
        throw ParseError("invalid exit model row");
      e.puzzle=puzzle!=0; e.enabled=enabled!=0; g.exits.push_back(e);
    } else throw ParseError("unknown navigation model row");
  }
  validate_navigation(g); return g;
}

bool exit_contains(const Exit& e,std::int32_t x,std::int32_t y) {
  return e.enabled && x>=e.left && x<e.right && y>=e.top && y<e.bottom;
}
std::optional<std::size_t> navigation_hit(const SceneGraph& g,std::uint32_t source,std::int32_t x,std::int32_t y) {
  for (std::size_t i=0;i<g.exits.size();++i) if (g.exits[i].source==source && exit_contains(g.exits[i],x,y)) return i;
  return std::nullopt;
}
std::map<std::uint32_t,std::filesystem::path> resolve_scene_assets(const std::filesystem::path& root,const SceneGraph& g) {
  validate_navigation(g);
  const auto lower=[](std::string s){for (auto& c:s) if (c>='A'&&c<='Z') c=char(c-'A'+'a'); return s;};
  std::map<std::string,std::uint32_t> wanted;
  for (const auto& node:g.nodes) wanted.emplace(lower(node.asset+".aqc"),node.id);
  std::map<std::uint32_t,std::filesystem::path> found;
  std::size_t visited=0;
  for (auto it=std::filesystem::recursive_directory_iterator(root);it!=std::filesystem::recursive_directory_iterator();++it) {
    if (++visited>10000 || it.depth()>32) throw ParseError("scene asset scan limit exceeded");
    if (it->is_symlink() || !it->is_regular_file()) continue;
    const auto match=wanted.find(lower(it->path().filename().string()));
    if (match==wanted.end()) continue;
    if (!found.emplace(match->second,std::filesystem::canonical(it->path())).second)
      throw ParseError("duplicate scene asset: "+it->path().filename().string());
  }
  for (const auto& node:g.nodes) if (!found.count(node.id))
    throw ParseError("missing scene asset "+node.asset+".AQC; import the complete original DATA folder");
  return found;
}
HotspotPoint hotspot_point(const PanoramaGeometry& g,TexturePoint p) {
  if (!g.width || !g.height || g.width>8192 || g.height>8192 || !std::isfinite(p.x) || !std::isfinite(p.y) ||
      g.yaw_min!=0 || g.yaw_max!=360 || !std::isfinite(g.pitch_min) || !std::isfinite(g.pitch_max) ||
      g.pitch_min< -90 || g.pitch_max>90 || g.pitch_min>=g.pitch_max) throw ParseError("invalid hotspot texture point");
  const auto u=std::floor(std::clamp(p.x,0.0,double(g.width)));
  const auto x=u==g.width ? 0 : u;
  const auto y=std::floor(std::clamp(p.y,0.0,double(g.height-1)));
  return {static_cast<std::int32_t>(x/g.width*3600),
          static_cast<std::int32_t>(((y-g.height/2.0)/g.height*(g.pitch_max-g.pitch_min)+(g.pitch_min+g.pitch_max)/2)*10)};
}
}
