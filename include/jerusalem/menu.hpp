#pragma once
#include "jerusalem/stage.hpp"
#include <array>

namespace jerusalem {
// Verified 640x480 menu registrations in the supported Jerusalem executable.
// Rectangles are half-open; highlight images extend four pixels below the hit box.
struct MenuEntry {
  std::uint32_t object;
  const char* label;
  const char* highlight;
  int left,top,right,bottom;
  unsigned image_width;
};
const std::array<MenuEntry,10>& original_menu_entries();
struct MenuArtwork {
  Bitmap background;
  std::array<std::optional<Bitmap>,10> highlights;
  std::vector<std::string> warnings;
};
// A bad/missing background is an error; optional highlights can fail independently.
MenuArtwork load_menu_art(const StageArt& art);
}
