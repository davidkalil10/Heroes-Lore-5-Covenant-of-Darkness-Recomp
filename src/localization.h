#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace Locale {
struct Glyph {int width=0,height=0,xoff=0,yoff=0,ascent=0;std::vector<unsigned char> alpha;};
void init(const std::string& resourceRoot,const std::string& dataRoot,const std::string& overrideLanguage="");
bool setLanguage(const std::string& language,bool persist=true);
const std::string& language();
bool portuguese();
std::string text(const std::string& english);
enum class UiText {Full,Grid,Tab,Stat,Hud};
std::string uiText(const std::string& english,UiText context);
const Glyph* glyph(uint32_t character,int height);
}
