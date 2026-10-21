#include "localization.h"
#include "service_data.h"
#include "vendor/json.hpp"
#include "vendor/stb_truetype.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <unordered_map>

namespace Locale {
static std::string current="pt-BR",settings;
static std::unordered_map<std::string,std::string> dictionary,cache;
static std::vector<unsigned char> fontBytes;
static stbtt_fontinfo font{};
static bool fontReady=false;
static std::map<std::pair<uint32_t,int>,Glyph> glyphs;
struct Pattern {std::regex match;std::string source,target;};
static std::vector<Pattern> patterns;
const std::string& language() {return current;}
bool portuguese() {return current=="pt-BR";}
bool setLanguage(const std::string& value,bool persist) {
    if(value!="en" && value!="pt-BR") return false;
    current=value;cache.clear();
    if(persist && !settings.empty()) {
        try {services::writeText(settings,"language="+current+"\n");} catch(...) {return false;}
    }
    return true;
}
void init(const std::string& resources,const std::string& data,const std::string& overrideLanguage) {
    current="pt-BR";dictionary.clear();cache.clear();glyphs.clear();
    settings=data+"/language.ini";
    try {
        std::ifstream input(settings);std::string line;
        while(std::getline(input,line)) {if(!line.empty() && line.back()=='\r') line.pop_back();if(line.rfind("language=",0)==0)setLanguage(line.substr(9),false);}
    } catch(...) {}
    if(!overrideLanguage.empty()) setLanguage(overrideLanguage,false);
    auto path=[&](const char* name) {return resources+(resources.empty() || resources.back()!='/'?"/":"")+name;};
    try {
        // Packaged with the executable: language changes never depend on a stale
        // extracted asset or on the process working directory.
        static const char compiledDictionary[]=
#include "locales_pt_br.inc"
        ;
        auto j=nlohmann::json::parse(compiledDictionary);
        dictionary=j.at("translations").get<decltype(dictionary)>();
    } catch(const std::exception& error) {fprintf(stderr,"[locale] Cannot load PT-BR: %s\n",error.what());}
    try {
        auto bytes=services::readText(path("ui_font.ttf"),4*1024*1024);fontBytes.assign(bytes.begin(),bytes.end());
        fontReady=stbtt_InitFont(&font,fontBytes.data(),0)!=0;
    } catch(...) {fontReady=false;}
    patterns.clear();
    static const std::regex placeholder(R"(%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[hlz]*[diuoxXfgesc])");
    for(const auto& item:dictionary) {
        if(item.first.find('%')==std::string::npos || item.first.size()>400) continue;
        std::string regex="^",source=item.first;size_t last=0;bool found=false;
        for(std::sregex_iterator it(source.begin(),source.end(),placeholder),end;it!=end;++it) {
            auto m=*it;
            for(size_t i=last;i<size_t(m.position());i++) {
                char c=source[i];if(std::string(".^$|()[]{}*+?\\").find(c)!=std::string::npos)regex+='\\';regex+=c;
            }
            regex+="(.+?)";last=m.position()+m.length();found=true;
        }
        if(!found || std::regex_replace(source,placeholder,"").size()<4) continue;
        for(size_t i=last;i<source.size();i++) {char c=source[i];if(std::string(".^$|()[]{}*+?\\").find(c)!=std::string::npos)regex+='\\';regex+=c;}
        try {patterns.push_back({std::regex(regex+"$"),source,item.second});} catch(...) {}
    }
    std::sort(patterns.begin(),patterns.end(),[](const Pattern& a,const Pattern& b){return a.source.size()>b.source.size();});
    fprintf(stderr,"[locale] %s, %zu translations, Latin font=%d\n",current.c_str(),dictionary.size(),fontReady);
}
static std::string piece(const std::string& source) {
    auto exact=dictionary.find(source);if(exact!=dictionary.end())return exact->second;
    size_t first=source.find_first_not_of(" \t\r\n^&#*|-~><@$%");
    size_t last=source.find_last_not_of(" \t\r\n");
    if(first!=std::string::npos) {
        auto found=dictionary.find(source.substr(first,last-first+1));
        if(found!=dictionary.end())return source.substr(0,first)+found->second+source.substr(last+1);
    }
    if(source.size()<500) for(const auto& pattern:patterns) {
        std::smatch match;if(!std::regex_match(source,match,pattern.match))continue;
        std::string result=pattern.target;size_t offset=0;
        static const std::regex token(R"(%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[hlz]*[diuoxXfgesc])");
        for(size_t index=1;index<match.size();index++) {
            std::smatch m;auto tail=result.substr(offset);if(!std::regex_search(tail,m,token))break;
            size_t pos=offset+m.position();result.replace(pos,m.length(),match[index].str());offset=pos+match[index].length();
        }
        return result;
    }
    return source;
}
std::string text(const std::string& source) {
    if(!portuguese() || source.empty())return source;
    auto cached=cache.find(source);if(cached!=cache.end())return cached->second;
    std::string result=piece(source);
    if(result==source && source.find_first_of(";\n")!=std::string::npos) {
        result.clear();size_t start=0;
        for(size_t i=0;i<=source.size();i++) if(i==source.size() || source[i]==';' || source[i]=='\n') {
            result+=piece(source.substr(start,i-start));if(i<source.size())result+=source[i];start=i+1;
        }
    }
    if(cache.size()>16384)cache.clear();
    if(source.size()<4096)cache.emplace(source,result);
    return result;
}
std::string uiText(const std::string& source,UiText context) {
    if(!portuguese() || context==UiText::Full)return text(source);
    static const std::unordered_map<std::string,std::string> grid={
        {"STATUS","ATRIB."},{"BAG","BOLSA"},{"EQUIP","EQUIP."},{"SKILL","HABIL."},
        {"MAGIC","MAGIA"},{"CHARGE","CARGA"},{"SPIRIT","ESPÍR."},{"QUEST","MISSÃO"},{"SYSTEM","SISTEMA"},
        {"SETTINGS","OPÇÕES"},{"UPDATE","ATUAL."},{"PROGRESS","PROGR."},{"STORAGE","BAÚ"},{"GAME END","SAIR"}};
    static const std::unordered_map<std::string,std::string> tabs={
        {"ALL","TODAS"},{"SKILL","HAB."},{"MAGIC","MAGIA"},{"SPIRIT","ESP."},{"MAIN","BASE"}};
    static const std::unordered_map<std::string,std::string> hud={
        {"MAP","Mapa"},{"SHOP","Comprar"},{"SAVE","Salvar"},{"map","Mapa"},{"shop","Comprar"},{"save","Salvar"}};
    const auto& labels=context==UiText::Grid?grid:context==UiText::Tab?tabs:context==UiText::Hud?hud:dictionary;
    auto found=labels.find(source);return found==labels.end()?text(source):found->second;
}
const Glyph* glyph(uint32_t character,int height) {
    if(!fontReady || character<32 || character>255)return nullptr;
    height=std::clamp(height,6,32);auto key=std::make_pair(character,height);
    auto found=glyphs.find(key);if(found!=glyphs.end())return &found->second;
    Glyph g;float scale=stbtt_ScaleForPixelHeight(&font,float(height));int ascent=0;
    stbtt_GetFontVMetrics(&font,&ascent,nullptr,nullptr);g.ascent=int(ascent*scale+.5f);
    auto* pixels=stbtt_GetCodepointBitmap(&font,0,scale,int(character),&g.width,&g.height,&g.xoff,&g.yoff);
    if(pixels) {g.alpha.assign(pixels,pixels+g.width*g.height);stbtt_FreeBitmap(pixels,nullptr);}
    return &glyphs.emplace(key,std::move(g)).first->second;
}
}
