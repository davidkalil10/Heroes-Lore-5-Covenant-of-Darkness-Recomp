#include "service_ui.h"
#include "services.h"
#include "localization.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace serviceui {
static bool flag(const char* name) {auto value=SDL_getenv(name);return value && *value;}
enum class Page {Settings,Cloud,Update,ConfirmUpload,ConfirmRestore,ConfirmLogout};
static bool opened=false,startupPrompt=false;
static Page page=Page::Settings;
static int selected=0,windowW=0,windowH=0;
static std::function<void()> cycle;
static std::function<AspectMode()> mode;
static std::vector<SDL_Rect> hits;
static SDL_Texture* fontTexture=nullptr;
static stbtt_bakedchar chars[224];
static std::vector<unsigned char> fontBitmap;
static bool fontReady=false,pointerConsumed=false,stickHeld=false;
static uint32_t nextCharacter(const std::string& text,size_t& i) {
    unsigned char c=text[i++];if(c<128) return c;
    if((c&0xe0)==0xc0 && i<text.size()) return ((c&31)<<6)|(text[i++]&63);
    while(i<text.size() && (text[i]&0xc0)==0x80) i++;
    return '?';
}
static float textWidth(const std::string& text,float size) {
    if(!fontReady || flag("HL5_UI_FONT_FALLBACK")) {float width=0;for(size_t i=0;i<text.size();){nextCharacter(text,i);width+=size*.6f;}return width;}
    float x=0;
    for(size_t i=0;i<text.size();) {auto c=nextCharacter(text,i);if(c<32||c>255)c='?';x+=chars[c-32].xadvance*size/32;}
    return x;
}
static void text(SDL_Renderer* r,const std::string& source,float x,float y,float size,SDL_Color color) {
    auto value=Locale::text(source);
    if(!fontTexture || flag("HL5_UI_FONT_FALLBACK")) {
        // Emergency built-in font keeps Settings readable on renderer/font failures.
        static const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-/?!+() ";
        static const char* pixels[]={
          "0111010001111111000110001","1111010001111101000111110","0111110000100001000001111","1111010001100011000111110",
          "1111110000111101000011111","1111110000111101000010000","0111110000101111000101111","1000110001111111000110001",
          "1111100100001000010011111","0011100010000101001001100","1000110010111001001010001","1000010000100001000011111",
          "1000111011101011000110001","1000111001101011001110001","0111010001100011000101110","1111010001111101000010000",
          "0111010001101011001001101","1111010001111101001010001","0111110000011100000111110","1111100100001000010000100",
          "1000110001100011000101110","1000110001100010101000100","1000110001101011101110001","1000101010001000101010001",
          "1000101010001000010000100","1111100010001000100011111","0111010011101011100101110","0010001100001000010001110",
          "0111010001000100010011111","1111000001001100000111110","1001010010111110001000010","1111110000111100000111110",
          "0111010000111101000101110","1111100010001000100001000","0111010001011101000101110","0111010001011110000101110",
          "0000000100000000010000000","0000000000000000000000100","0000000000111110000000000","0000100010001000100010000",
          "0111010001000100000000010","0010000100001000000000100","0000000100011100010000000","0001000100001000010000010",
          "0100000100001000010001000","0000000000000000000000000"};
        SDL_SetRenderDrawColor(r,color.r,color.g,color.b,color.a);float scale=std::max(1.0f,size/7);
        for(size_t i=0;i<value.size();) {
            auto c=nextCharacter(value,i);
            if(c>=0xe0 && c<=0xe5)c='A';else if(c==0xe7)c='C';else if(c>=0xe8&&c<=0xeb)c='E';
            else if(c>=0xec&&c<=0xef)c='I';else if(c>=0xf2&&c<=0xf6)c='O';else if(c>=0xf9&&c<=0xfc)c='U';
            if(c>='a'&&c<='z')c-=32;
            if(c>=0xc0&&c<=0xc5)c='A';else if(c==0xc7)c='C';else if(c>=0xc8&&c<=0xcb)c='E';
            else if(c>=0xcc&&c<=0xcf)c='I';else if(c>=0xd2&&c<=0xd6)c='O';else if(c>=0xd9&&c<=0xdc)c='U';
            const char* found=c<128?strchr(alphabet,int(c)):nullptr;
            size_t index=found?size_t(found-alphabet):40;
            for(int row=0;row<5;row++)for(int col=0;col<5;col++)if(pixels[index][row*5+col]=='1') {
                SDL_FRect rect{x+col*scale,y+2+row*scale,scale,scale};SDL_RenderFillRectF(r,&rect);
            }
            x+=size*.6f;
        }
        return;
    }
    SDL_SetTextureColorMod(fontTexture,color.r,color.g,color.b);SDL_SetTextureAlphaMod(fontTexture,color.a);
    float scale=size/32;
    for(size_t i=0;i<value.size();) {
        auto c=nextCharacter(value,i);if(c<32||c>255)c='?';const auto& g=chars[c-32];
        SDL_Rect src{int(g.x0),int(g.y0),g.x1-g.x0,g.y1-g.y0};
        SDL_FRect dst{x+g.xoff*scale,y+size+g.yoff*scale,src.w*scale,src.h*scale};
        SDL_RenderCopyF(r,fontTexture,&src,&dst);x+=g.xadvance*scale;
    }
}
static int wrapped(SDL_Renderer* r,const std::string& source,int x,int y,int width,int size,int lines=6) {
    auto value=Locale::text(source);
    std::string line,word;int count=0;
    auto emit=[&]() {if(count<lines) text(r,line,float(x),float(y+count*(size+5)),float(size),{210,224,239,255});line.clear();count++;};
    for(size_t i=0;i<=value.size();i++) {
        char c=i<value.size()?value[i]:'\n';
        if(c==' ' || c=='\n') {
            if(!line.empty() && textWidth(line+" "+word,float(size))>width) emit();
            if(!line.empty()) line+=' ';
            line+=word;word.clear();if(c=='\n') emit();
        } else word+=c;
    }
    return std::min(lines,count)*(size+5);
}
void init(const std::string& fontPath,std::function<void()> cycleAspect,std::function<AspectMode()> currentMode) {
    cycle=std::move(cycleAspect);mode=std::move(currentMode);
    try {
        auto bytes=services::readText(fontPath,4*1024*1024);fontBitmap.resize(1024*1024);
        fontReady=stbtt_BakeFontBitmap((const unsigned char*)bytes.data(),0,32,fontBitmap.data(),1024,1024,32,224,chars)>0;
    } catch(...) {fontReady=false;}
}
void shutdown() {if(fontTexture) SDL_DestroyTexture(fontTexture);fontTexture=nullptr;fontBitmap.clear();opened=false;}
bool active() {return opened;}
void poll() {
    if(!opened && services::takeUpdateNotice()){open();startupPrompt=true;page=Page::Update;}
}
void open() {startupPrompt=false;opened=true;page=Page::Settings;selected=0;SDL_Log("[settings] opened by explicit action");}
void restoredToTitle() {opened=false;hits.clear();}
static void go(Page next) {page=next;selected=0;hits.clear();}
static std::vector<std::string> rows(const services::Status& s) {
    if(s.restartRequired) return {"Close game - reopen to continue"};
    if(s.busy) return {s.userCode.empty()?"Please wait...":"Cancel login"};
    switch(page) {
        case Page::Settings:return {std::string("Aspect ratio: ")+aspectModeName(mode()),
            std::string("Check updates at startup: ")+(s.autoCheck?"ON":"OFF"),"Check for updates","Cloud save - Google Drive",std::string("Language: ")+(Locale::portuguese()?"Português (Brasil)":"English"),"Back to game"};
        case Page::Cloud:return s.connected?std::vector<std::string>{"Refresh cloud backup","Upload saved progress","Restore cloud backup","Disconnect Google Drive","Back"}:
            std::vector<std::string>{"Connect Google Drive","Back"};
        case Page::Update:
            if(s.downloadReady) return {"Install update","Back"};
            if(s.updateAvailable) return {"Download "+s.release.tag,"Check again","Back"};
            return {"Check again","Back"};
        case Page::ConfirmUpload:return {"Confirm upload - replace cloud backup","Cancel"};
        case Page::ConfirmRestore:return {"Confirm restore and return to title","Cancel"};
        case Page::ConfirmLogout:return {"Disconnect this device","Cancel"};
    }
    return {};
}
static void back() {
    auto s=services::status();
    if(s.restartRequired) return;
    if(s.busy) {if(!s.userCode.empty()) services::cancelLogin();return;}
    if(page==Page::Update && startupPrompt){opened=false;startupPrompt=false;return;}
    if(page==Page::Settings) opened=false;
    else if(page==Page::ConfirmUpload || page==Page::ConfirmRestore || page==Page::ConfirmLogout) go(Page::Cloud);
    else go(Page::Settings);
}
static void activate() {
    auto s=services::status();
    if(s.restartRequired) {services::requestQuit();return;}
    if(s.busy) {if(!s.userCode.empty()) services::cancelLogin();return;}
    if(page==Page::Settings) {
        switch(selected) {
            case 0:cycle();break;
            case 1:services::setAutoCheck(!s.autoCheck);break;
            case 2:go(Page::Update);services::checkUpdate(true);break;
            case 3:go(Page::Cloud);if(s.connected) services::refreshBackup();break;
            case 4:Locale::setLanguage(Locale::portuguese()?"en":"pt-BR");break;
            case 5:opened=false;break;
        }
    } else if(page==Page::Cloud) {
        if(!s.connected) {if(selected==0) services::login();else go(Page::Settings);return;}
        switch(selected) {
            case 0:services::refreshBackup();break;
            case 1:go(Page::ConfirmUpload);break;
            case 2:if(s.backupExists) go(Page::ConfirmRestore);break;
            case 3:go(Page::ConfirmLogout);break;
            case 4:go(Page::Settings);break;
        }
    } else if(page==Page::ConfirmUpload) {int choice=selected;go(Page::Cloud);if(choice==0) services::uploadBackup();}
    else if(page==Page::ConfirmRestore) {int choice=selected;go(Page::Cloud);if(choice==0) services::restoreBackup();}
    else if(page==Page::ConfirmLogout) {int choice=selected;go(Page::Cloud);if(choice==0) services::logout();}
    else if(page==Page::Update) {
        if(s.downloadReady) {if(selected==0) services::applyUpdate();else back();}
        else if(s.updateAvailable) {if(selected==0) services::downloadUpdate();else if(selected==1) services::checkUpdate(true);else back();}
        else {if(selected==0) services::checkUpdate(true);else back();}
    }
}
bool event(const SDL_Event& e,bool switchButtons) {
    if(!opened) {
        if(pointerConsumed && (e.type==SDL_MOUSEBUTTONUP || e.type==SDL_FINGERUP)) {pointerConsumed=false;return true;}
        if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_F10 && !e.key.repeat) {open();return true;}
        if(e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSTICK) {open();return true;}
        return false;
    }
    if(e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_FINGERDOWN) pointerConsumed=true;
    bool up=false,down=false,confirm=false,cancel=false;
    if(e.type==SDL_CONTROLLERAXISMOTION && e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY) {
        if(std::abs(int(e.caxis.value))<8000) stickHeld=false;
        else if(std::abs(int(e.caxis.value))>16000 && !stickHeld) {up=e.caxis.value<0;down=!up;stickHeld=true;}
    }
    if(e.type==SDL_KEYDOWN && !e.key.repeat) {
        up=e.key.keysym.sym==SDLK_UP || e.key.keysym.sym==SDLK_w;
        down=e.key.keysym.sym==SDLK_DOWN || e.key.keysym.sym==SDLK_s;
        confirm=e.key.keysym.sym==SDLK_RETURN || e.key.keysym.sym==SDLK_SPACE || e.key.keysym.sym==SDLK_z;
        cancel=e.key.keysym.sym==SDLK_ESCAPE || e.key.keysym.sym==SDLK_x || e.key.keysym.sym==SDLK_F10;
    } else if(e.type==SDL_CONTROLLERBUTTONDOWN) {
        up=e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP;down=e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        confirm=e.cbutton.button==(switchButtons?SDL_CONTROLLER_BUTTON_B:SDL_CONTROLLER_BUTTON_A);
        cancel=e.cbutton.button==(switchButtons?SDL_CONTROLLER_BUTTON_A:SDL_CONTROLLER_BUTTON_B) || e.cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    }
    if(cancel) back();
    else if(up || down) {
        auto count=int(rows(services::status()).size());selected=(selected+(up?-1:1)+count)%count;
    } else if(confirm) activate();
    else {
        int x=-1,y=-1;
        if(e.type==SDL_MOUSEBUTTONDOWN) {x=e.button.x;y=e.button.y;}
        else if(e.type==SDL_FINGERDOWN) {x=int(e.tfinger.x*windowW);y=int(e.tfinger.y*windowH);}
        for(size_t i=0;i<hits.size();i++) if(insideRect(x,y,hits[i])) {selected=int(i);activate();break;}
    }
    return true; // A modal consumes game input, including releases and touch gestures.
}
static bool mobileLayout() {
#ifdef __ANDROID__
    return true;
#else
    return flag("HL5_UI_LAYOUT_ANDROID");
#endif
}
void draw(SDL_Renderer* r,int width,int height) {
    windowW=width;windowH=height;
    if(!fontTexture && fontReady) {
        std::vector<uint32_t> rgba(fontBitmap.size());
        for(size_t i=0;i<rgba.size();i++) rgba[i]=(uint32_t(fontBitmap[i])<<24)|0xffffff;
        auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(rgba.data(),1024,1024,32,4096,SDL_PIXELFORMAT_ARGB8888);
        if(surface) {fontTexture=SDL_CreateTextureFromSurface(r,surface);SDL_FreeSurface(surface);}
        if(fontTexture) SDL_SetTextureBlendMode(fontTexture,SDL_BLENDMODE_BLEND);
        else SDL_Log("Settings font texture: %s",SDL_GetError());
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    auto s=services::status();
    if(!opened) {
        if(s.checkingUpdates) {
            int font=std::clamp(width/45,14,24);
            SDL_Rect banner{12,height-font-30,width-24,font+18};
            SDL_SetRenderDrawColor(r,18,27,42,235);SDL_RenderFillRect(r,&banner);
            text(r,"Checking GitHub releases...",22,float(banner.y+3),float(font),{230,238,245,255});
        }
        return;
    }
    SDL_SetRenderDrawColor(r,0,0,0,185);SDL_RenderFillRect(r,nullptr);
    int panelW=std::min(740,std::max(180,width-24)),panelH=std::min(650,std::max(180,height-24));
    if(mobileLayout()) {panelW=std::min(width*94/100,height*3/2);panelH=height*(height>width?76:94)/100;}
    SDL_Rect panel{(width-panelW)/2,(height-panelH)/2,panelW,panelH};
    SDL_SetRenderDrawColor(r,18,27,42,250);SDL_RenderFillRect(r,&panel);
    SDL_SetRenderDrawColor(r,97,157,190,255);SDL_RenderDrawRect(r,&panel);
    int pad=std::max(10,panelW/32),x=panel.x+pad,y=panel.y+pad;
    int font=std::clamp(panelW/27,13,23),rowH=std::clamp(panelH/16,25,42);
    if(mobileLayout()) {font=std::max(13,std::min(panelW/26,panelH/22));rowH=font*2;}
    std::string title=page==Page::Settings?"SETTINGS":page==Page::Update?"APP UPDATE":"CLOUD SAVE - GOOGLE DRIVE";
    text(r,title,float(x),float(y),float(font+3),{241,205,115,255});
    y+=font+16;
    std::string info;
    if(page==Page::Settings) info="Heroes Lore 5 | "+Locale::text("Version")+" "+std::string(services::Version);
    else if(page==Page::ConfirmRestore) info="This replaces local saved progress with the cloud backup. A local safety copy is kept. You will return to the title screen to load the restored save.";
    else if(page==Page::ConfirmUpload) info="Upload progress already saved in the game. The previous cloud backup will be replaced. Save your game first.";
    else if(page==Page::ConfirmLogout) info="Remove the Google login from this device. The backup in Drive is kept.";
    else if(page==Page::Cloud) {
        info=s.connected?"Google Drive connected. Save in the game before uploading.":"Continue your Heroes Lore 5 progress on another platform using the same Google account.";
        if(s.backupExists) info+="\nCloud: "+s.backupDate+"\n"+s.backupSummary;
    } else info=Locale::text("Installed:")+" "+std::string(services::Version)+(s.updateAvailable?" | "+Locale::text("Available:")+" "+s.release.tag:"")+"\n"+Locale::text("Updates preserve your saved progress and Google login.");
    y+=wrapped(r,info,x,y,panelW-pad*2,font-1,4)+10;
    if(!s.userCode.empty()) {
        y+=wrapped(r,s.verificationUrl,x,y,panelW-pad*2,font,2)+5;
        text(r,s.userCode,float(x),float(y),float(font+10),{115,214,255,255});y+=font+25;
    }
    auto labels=rows(s);for(auto& label:labels)label=Locale::text(label);selected=std::clamp(selected,0,int(labels.size())-1);
    hits.clear();
    for(size_t i=0;i<labels.size();i++) {
        SDL_Rect rect{x,y,panelW-pad*2,rowH};hits.push_back(rect);
        SDL_SetRenderDrawColor(r,i==size_t(selected)?46:29,i==size_t(selected)?93:47,i==size_t(selected)?126:67,255);
        SDL_RenderFillRect(r,&rect);
        float size=float(font);while(size>10 && textWidth(labels[i],size)>rect.w-20) size--;
        text(r,labels[i],float(x+10),float(y+(rowH-int(size))/2-3),size,{245,248,255,255});
        y+=rowH+5;
    }
    y+=8;
    if(s.downloading) {
        int percent=s.totalBytes?int(std::min<uint64_t>(100,s.downloadedBytes*100/s.totalBytes)):0;
        char detail[96];snprintf(detail,sizeof(detail),"%d%%  |  %.1f / %.1f MB",percent,double(s.downloadedBytes)/1048576,double(s.totalBytes)/1048576);
        text(r,detail,float(x),float(y),float(font),{220,235,250,255});y+=font+12;
        SDL_Rect track{x,y,panelW-pad*2,std::max(14,font)};
        SDL_SetRenderDrawColor(r,12,20,32,255);SDL_RenderFillRect(r,&track);
        SDL_Rect fill{track.x,track.y,track.w*percent/100,track.h};
        SDL_SetRenderDrawColor(r,63,191,223,255);SDL_RenderFillRect(r,&fill);
        SDL_SetRenderDrawColor(r,103,147,176,255);SDL_RenderDrawRect(r,&track);y+=track.h+14;
    }
    wrapped(r,s.message,x,y,panelW-pad*2,std::max(12,font-2),std::max(1,(panel.y+panel.h-font-20-y)/(font+3)));
    text(r,"Arrows / D-pad: move     Confirm: select     Back: close",float(x),float(panel.y+panel.h-font-10),float(std::max(10,font-6)),{150,175,198,255});
}
}
