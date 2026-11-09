#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace jerusalem {
// aApplication methods (and two CRT helpers) that the original area scripts call.
// Names follow the original's own log strings where it has them. `args` counts
// stack arguments; `callee_pops` is false for cdecl functions.
struct EngineApi { std::uint32_t address; std::string_view name; unsigned args; bool callee_pops; };

inline constexpr EngineApi engine_apis[]{
  {0x0040a360,"Log",2,true},
  {0x0040a420,"Log2",3,true},
  {0x00427140,"SetMode5F",1,true},
  {0x00427f60,"GameCall",2,true},
  {0x004280a0,"SetSpace",1,true},
  {0x00428120,"SetSpaceSoft",1,true},
  {0x00428170,"GetSpace",0,true},
  {0x004282a0,"PlayMovie",2,true},
  {0x004284d0,"FadOut",5,true},
  {0x00428920,"ImgTransition",6,true},
  {0x004292c0,"GoPuz",3,true},
  {0x004293e0,"RotSetAct",3,true},
  {0x00429550,"InRotation",0,true},
  {0x00429560,"CurPuz",0,true},
  {0x00429570,"CurRot",0,true},
  {0x004296f0,"Refresh",0,true},
  {0x00429d50,"ObjAccOn",1,true},
  {0x00429d70,"ObjAccOff",1,true},
  {0x00429d90,"ObjAccRangeOn",3,true},
  {0x00429db0,"ObjAccRangeOff",3,true},
  {0x0042a540,"ObjPreGetX",3,true},
  {0x0042a580,"ObjPreGetY",3,true},
  {0x0042a6f0,"ObjPrePauAni",2,true},
  {0x0042a730,"ObjPreUnPauAni",2,true},
  {0x0042a770,"ObjPrePauTimAni",3,true},
  {0x0042a920,"ObjPreSho",2,true},
  {0x0042a9a0,"ObjPreHid",2,true},
  {0x0042aa20,"ObjPreHidDeaPuz",2,true},
  {0x0042aaa0,"ObjShoAll",1,true},
  {0x0042ab20,"ObjHidAll",1,true},
  {0x0042b430,"PuzModOn",1,true},
  {0x0042b440,"PuzModOff",1,true},
  {0x0042bf70,"RotConRangeOn",3,true},
  {0x0042bf90,"RotConRangeOff",3,true},
  {0x0042bfb0,"RotSetAlp",2,true},
  {0x0042c170,"VarDefByte",2,true},
  {0x0042c1a0,"VarSetByte",2,true},
  {0x0042c1d0,"VarGetByte",1,true},
  {0x0042c1f0,"VarDefWord",2,true},
  {0x0042c220,"VarSetWord",2,true},
  {0x0042c250,"VarGetWord",1,true},
  {0x0042c270,"VarDefDword",2,true},
  {0x0042c2a0,"VarSetDword",2,true},
  {0x0042c2d0,"VarGetDword",1,true},
  {0x0042c2f0,"VarDefFloat",2,true},
  {0x0042c320,"VarSetFloat",2,true},
  {0x0042c350,"VarGetFloat",1,true},
  {0x0042c3a0,"BagAdd",1,true},
  {0x0042c460,"BagRem",1,true},
  {0x0042c4f0,"BagRemAll",0,true},
  {0x0042c550,"BagIsIn",1,true},
  {0x0042c5e0,"HasHeld",0,true},
  {0x0042c600,"HeldItem",0,true},
  {0x0042c620,"ReleaseHeld",0,true},
  {0x0042c670,"TimSta",2,true},
  {0x0042c6b0,"TimSto",1,true},
  {0x0042c6e0,"TimStoAll",0,true},
  {0x0042c710,"TimIsOn",1,true},
  {0x0042c730,"ClearFlag6B",0,true},
  {0x0042c740,"EncSetMode",1,true},
  {0x0042c790,"EncRowBefore",0,true},
  {0x0042c7b0,"EncRowAt",0,true},
  {0x0042c7d0,"EncRowAfter",0,true},
  {0x0042c7f0,"EncColBefore",0,true},
  {0x0042c810,"EncColAt",0,true},
  {0x0042c830,"EncColAfter",0,true},
  {0x0042c850,"EncStep",0,true},
  {0x0042ccd0,"Speak",2,true},
  {0x0042cd50,"SouSetVol",2,true},
  {0x0042cdd0,"SouStop",1,true},
  {0x0042cdf0,"SouAnyPlaying",0,true},
  {0x0042ce00,"SouIsPlaying",1,true},
  {0x00431690,"Credits",0,true},
  {0x0048a5a0,"BoxShow",5,true},
  {0x0048a610,"BoxHide",2,true},
  {0x0049097b,"rand",0,false},
};

// Variable APIs: kind (0 def, 1 set, 2 get) and size in bytes (4 also for float).
struct VariableApi { unsigned kind,size; };
inline std::optional<VariableApi> variable_api(std::uint32_t address) {
  if (address<0x42c170 || address>0x42c350) return std::nullopt;
  constexpr std::uint32_t table[12]{0x42c170,0x42c1a0,0x42c1d0,0x42c1f0,0x42c220,0x42c250,0x42c270,0x42c2a0,0x42c2d0,0x42c2f0,0x42c320,0x42c350};
  for (unsigned i=0;i<12;++i) if (table[i]==address) return VariableApi{i%3,i<3?1U:i<6?2U:4U};
  return std::nullopt;
}

inline const EngineApi* find_engine_api(std::uint32_t address) {
  for (const auto& api:engine_apis) if (api.address==address) return &api;
  return nullptr;
}
}  // namespace jerusalem
