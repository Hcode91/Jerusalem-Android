#include "jerusalem/aqc_animation.hpp"
#include "aqc_fixture.hpp"
#include <functional>
#include <iostream>
#include <limits>

using namespace jerusalem;
namespace {
int checks=0;
void check(bool value) { ++checks; if (!value) throw std::runtime_error("animation check failed: "+std::to_string(checks)); }
void rejects(const std::function<void()>& f) {
  bool rejected=false; try { f(); } catch (const ParseError&) { rejected=true; } check(rejected);
}
}
int main() {
  try {
    const auto data=fixture::animated();
    const auto channels=parse_aqc_channels(Bytes(data));
    check(channels.size()==2 && channels[0].frames.size()==1 && channels[1].frames.size()==2);
    check(channels[1].timing_value==12 && channels[1].frames[1].left==4 && channels[1].frames[1].top==2);
    const AqcAnimation animation(data);
    const auto patch=animation.frame(1,0);
    check(patch.width==4 && patch.height==2);
    for (std::size_t i=0;i<patch.rgba.size();i+=4)
      check(patch.rgba[i]==248 && patch.rgba[i+1]==252 && patch.rgba[i+2]==248 && patch.rgba[i+3]==255);
    rejects([&]{animation.frame(2,0);}); rejects([&]{animation.frame(1,2);});
    const auto base=decode_aqc(Bytes(data));
    auto target=base;
    apply_aqc_patch(target,patch,0,1);
    check(target.rgba!=base.rgba && target.rgba[0]==base.rgba[0]);
    rejects([&]{apply_aqc_patch(target,patch,8,0);});
    rejects([&]{apply_aqc_patch(target,patch,0,4);});
    rejects([&]{apply_aqc_patch(target,patch,0xffffffff,0);});
    auto bad=patch; bad.rgba.pop_back(); rejects([&]{apply_aqc_patch(target,bad,0,0);});
    check(aqc_preview_frame(0,2)==0 && aqc_preview_frame(83,2)==0 && aqc_preview_frame(84,2)==1);
    check(aqc_preview_frame(166,2)==1 && aqc_preview_frame(167,2)==0);
    check(aqc_preview_frame(1249,15)==14 && aqc_preview_frame(1250,15)==0);
    check(aqc_preview_frame(std::numeric_limits<std::uint64_t>::max(),15)<15);
    rejects([]{aqc_preview_frame(0,0);}); rejects([]{aqc_preview_frame(0,1001);});
    rejects([]{aqc_preview_frame(0,2,0);}); rejects([]{aqc_preview_frame(0,2,61);});
    AqcPreview preview(data,base);
    target=base;
    check(!preview.update(target,1000));
    check(preview.next() && preview.channel()==1);
    check(preview.update(target,0) && preview.frame()==0 && target.rgba!=base.rgba);
    check(!preview.update(target,83));
    check(preview.update(target,1) && preview.frame()==1 && target.rgba==base.rgba);
    check(preview.update(target,83) && preview.frame()==0 && target.rgba!=base.rgba);
    check(!preview.next() && !preview.channel());
    check(preview.update(target,0) && target.rgba==base.rgba);
    check(!preview.update(target,1000));
    check(preview.next() && preview.update(target,0));
    bad=base; bad.width=4; rejects([&]{AqcPreview invalid(data,bad);});
    // Every cut inside a record must fail; boundaries can represent a valid prefix.
    const auto start=parse_aqc(Bytes(data)).static_end;
    const auto boundary=channels[1].frames[0].header_offset-12;
    for (std::size_t end=start+1;end<data.size();++end) if (end!=boundary)
      rejects([&]{parse_aqc_channels(Bytes(data.data(),end));});
    std::cout << checks << " animation checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
