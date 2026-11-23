#pragma once
#include "jerusalem/resources.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace jerusalem {
// A small interpreter for the 32-bit integer code of the original game's per-area
// story handlers. It runs the user's own executable image in a sandbox: only the
// mapped PE sections, a private stack and a scratch area are addressable, and every
// call into an engine API function is trapped to a native callback. Floating point,
// system instructions, I/O and self-modifying code are rejected.
class X86Machine {
 public:
  struct Call {
    std::uint32_t target, this_pointer;
    std::vector<std::uint32_t> args;
  };
  // A trapped function receives its arguments and returns EAX.
  using Native = std::function<std::uint32_t(const Call&)>;

  explicit X86Machine(Bytes executable);
  // Trap a function taking `args` stack arguments. `callee_pops` is true for
  // thiscall/stdcall (the original ends in `ret 4*args`) and false for cdecl.
  void trap(std::uint32_t address, Native native, unsigned args, bool callee_pops=true);
  bool trapped(std::uint32_t address) const { return traps_.count(address)!=0; }
  // Calls `function` with `this` in ECX and stack arguments (pushed right to left).
  std::uint32_t call(std::uint32_t function, std::uint32_t this_pointer, const std::vector<std::uint32_t>& args, std::uint64_t step_limit=2000000);

  std::uint32_t scratch(std::size_t bytes);  // zeroed sandbox memory, freed on reset_scratch()
  void reset_scratch();
  std::uint8_t read8(std::uint32_t address) const;
  std::uint32_t read32(std::uint32_t address) const;
  void write8(std::uint32_t address, std::uint8_t value);
  void write32(std::uint32_t address, std::uint32_t value);
  std::string read_string(std::uint32_t address, std::size_t limit=260) const;
  std::uint64_t steps() const { return steps_; }
  // The mapped PE image (globals included), for saving and restoring script state.
  const std::vector<std::uint8_t>& image() const { return regions_.front().bytes; }
  void restore_image(const std::vector<std::uint8_t>& bytes);

 private:
  struct Region { std::uint32_t base; std::vector<std::uint8_t> bytes; bool writable; };
  struct Trap { Native native; unsigned args; bool callee_pops; };
  std::vector<Region> regions_;
  std::map<std::uint32_t, Trap> traps_;
  std::uint32_t reg_[8]{}, eip_=0, last_call_=0;
  bool cf_=false, zf_=false, sf_=false, of_=false;
  std::uint64_t steps_=0;
  std::size_t scratch_used_=0;
  std::uint8_t* at(std::uint32_t address, std::size_t size, bool write);
  const std::uint8_t* at(std::uint32_t address, std::size_t size) const;
  void run(std::uint32_t stop, std::uint64_t step_limit);
  void step();
  friend struct X86Decoder;
};
}  // namespace jerusalem
