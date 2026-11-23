#include "jerusalem/menu.hpp"

namespace jerusalem {
const std::array<MenuEntry,10>& original_menu_entries() {
  static const std::array<MenuEntry,10> entries{{
    {99001,"New Game","MEN1_H.BMP",269,28,433,64,164},
    {99002,"Resume Game","MEN2_H.BMP",269,64,469,100,200},
    {99004,"Load Game","MENULOAD.BMP",269,100,449,136,180},
    {99003,"Save Game","MENUSAVE.BMP",269,136,437,172,168},
    {99005,"Options","MEN5_H.BMP",269,242,405,278,136},
    {90200,"Visit","MENUVISIT.BMP",269,278,377,314,108},
    {90210,"Encyclopedia","MENUENC.BMP",269,314,449,350,180},
    {99009,"Intro","MEN_INTRO.BMP",269,350,377,386,112},
    {99010,"Credits","MEN_CRED.BMP",269,386,377,422,136},
    {99007,"Quit","MEN7_H.BMP",269,422,373,458,104}
  }};
  return entries;
}
MenuArtwork load_menu_art(const StageArt& art) {
  MenuArtwork out;out.background=art.image("MENU.BMP");
  if(out.background.width!=640 || out.background.height!=480)
    throw ParseError("original MENU.BMP must be 640x480");
  const auto& entries=original_menu_entries();
  for(std::size_t i=0;i<entries.size();++i) {
    try {
      auto bitmap=art.image(entries[i].highlight);
      if(bitmap.width!=entries[i].image_width || bitmap.height!=40)
        throw ParseError("unexpected menu highlight dimensions");
      out.highlights[i]=std::move(bitmap);
    }catch(const ParseError& e){out.warnings.push_back(std::string(entries[i].highlight)+": "+e.what());}
  }
  return out;
}
}
