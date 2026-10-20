#include "service_ui.h"
#include "services.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace serviceui {
enum class Page {Settings,Cloud,Update,ConfirmUpload,ConfirmRestore,ConfirmLogout};
static bool opened=false,guestSettings=false,wasGuestSettings=false;
static Page page=Page::Settings;
static int selected=0,windowW=0,windowH=0;
static std::function<void()> cycle;
static std::function<AspectMode()> mode;
static std::vector<SDL_Rect> hits;
static SDL_Texture* fontTexture=nullptr;
static stbtt_bakedchar chars[224];
static std::vector<unsigned char> fontBitmap;
static bool fontReady=false,pointerConsumed=false,stickHeld=false;
static SDL_Rect settingsButton(int w,int h) {
    int s=std::clamp(std::min(w,h)/360,1,3);return {8,8,88*s,22*s};
}
static uint32_t nextCharacter(const std::string& text,size_t& i) {
    unsigned char c=text[i++];if(c<128) return c;
    if((c&0xe0)==0xc0 && i<text.size()) return ((c&31)<<6)|(text[i++]&63);
    while(i<text.size() && (text[i]&0xc0)==0x80) i++;
    return '?';
}
static float textWidth(const std::string& text,float size) {
    float x=0;
    for(size_t i=0;i<text.size();) {auto c=nextCharacter(text,i);if(c<32||c>255)c='?';x+=chars[c-32].xadvance*size/32;}
    return x;
}
static void text(SDL_Renderer* r,const std::string& value,float x,float y,float size,SDL_Color color) {
    if(!fontTexture) return;
    SDL_SetTextureColorMod(fontTexture,color.r,color.g,color.b);SDL_SetTextureAlphaMod(fontTexture,color.a);
    float scale=size/32;
    for(size_t i=0;i<value.size();) {
        auto c=nextCharacter(value,i);if(c<32||c>255)c='?';const auto& g=chars[c-32];
        SDL_Rect src{int(g.x0),int(g.y0),g.x1-g.x0,g.y1-g.y0};
        SDL_FRect dst{x+g.xoff*scale,y+size+g.yoff*scale,src.w*scale,src.h*scale};
        SDL_RenderCopyF(r,fontTexture,&src,&dst);x+=g.xadvance*scale;
    }
}
static int wrapped(SDL_Renderer* r,const std::string& value,int x,int y,int width,int size,int lines=6) {
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
void open() {opened=true;page=Page::Settings;selected=0;}
void beginGuestFrame() {guestSettings=false;}
void notifyGameSettings() {guestSettings=true;}
void endGuestFrame() {if(guestSettings && !wasGuestSettings) open();wasGuestSettings=guestSettings;}
static void go(Page next) {page=next;selected=0;hits.clear();}
static std::vector<std::string> rows(const services::Status& s) {
    if(s.restartRequired) return {"Close game - reopen to continue"};
    if(s.busy) return {s.userCode.empty()?"Please wait...":"Cancel login"};
    switch(page) {
        case Page::Settings:return {"Game settings",std::string("Aspect ratio: ")+aspectModeName(mode()),
            std::string("Check updates at startup: ")+(s.autoCheck?"ON":"OFF"),"Check for updates","Cloud save - Google Drive","Back to game"};
        case Page::Cloud:return s.connected?std::vector<std::string>{"Refresh cloud backup","Upload saved progress","Restore cloud backup","Disconnect Google Drive","Back"}:
            std::vector<std::string>{"Connect Google Drive","Back"};
        case Page::Update:
            if(s.downloadReady) return {"Install update","Back"};
            if(s.updateAvailable) return {"Download "+s.release.tag,"Check again","Back"};
            return {"Check again","Back"};
        case Page::ConfirmUpload:return {"Confirm upload - replace cloud backup","Cancel"};
        case Page::ConfirmRestore:return {"Confirm restore and restart","Cancel"};
        case Page::ConfirmLogout:return {"Disconnect this device","Cancel"};
    }
    return {};
}
static void back() {
    auto s=services::status();
    if(s.restartRequired) return;
    if(s.busy) {if(!s.userCode.empty()) services::cancelLogin();return;}
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
            case 0:opened=false;break;
            case 1:cycle();break;
            case 2:services::setAutoCheck(!s.autoCheck);break;
            case 3:go(Page::Update);services::checkUpdate(true);break;
            case 4:go(Page::Cloud);if(s.connected) services::refreshBackup();break;
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
        if(s.downloadReady) {if(selected==0) services::applyUpdate();else go(Page::Settings);}
        else if(s.updateAvailable) {if(selected==0) services::downloadUpdate();else if(selected==1) services::checkUpdate(true);else go(Page::Settings);}
        else {if(selected==0) services::checkUpdate(true);else go(Page::Settings);}
    }
}
bool event(const SDL_Event& e,bool switchButtons) {
    if(!opened) {
        if(pointerConsumed && (e.type==SDL_MOUSEBUTTONUP || e.type==SDL_FINGERUP)) {pointerConsumed=false;return true;}
        if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_F10 && !e.key.repeat) {open();return true;}
        if(e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSTICK) {open();return true;}
        int x=-1,y=-1;
        if(e.type==SDL_MOUSEBUTTONDOWN) {x=e.button.x;y=e.button.y;}
        else if(e.type==SDL_FINGERDOWN) {x=int(e.tfinger.x*windowW);y=int(e.tfinger.y*windowH);}
        if(insideRect(x,y,settingsButton(windowW,windowH))) {open();return true;}
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
void draw(SDL_Renderer* r,int width,int height) {
    windowW=width;windowH=height;
    if(!fontTexture && fontReady) {
        std::vector<uint32_t> rgba(fontBitmap.size());
        for(size_t i=0;i<rgba.size();i++) rgba[i]=(uint32_t(fontBitmap[i])<<24)|0xffffff;
        fontTexture=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,1024,1024);
        if(fontTexture) {SDL_UpdateTexture(fontTexture,nullptr,rgba.data(),1024*4);SDL_SetTextureBlendMode(fontTexture,SDL_BLENDMODE_BLEND);}
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    auto button=settingsButton(width,height);
    SDL_SetRenderDrawColor(r,12,20,30,220);SDL_RenderFillRect(r,&button);
    text(r,"SETTINGS",float(button.x+8),float(button.y),float(button.h*.72),{240,246,252,255});
    auto s=services::status();
    if(s.updateAvailable && !opened) {SDL_SetRenderDrawColor(r,255,200,75,255);SDL_Rect badge{button.x+button.w-8,button.y+2,6,6};SDL_RenderFillRect(r,&badge);}
    if(!opened) return;
    SDL_SetRenderDrawColor(r,0,0,0,185);SDL_RenderFillRect(r,nullptr);
    int panelW=std::min(740,std::max(180,width-24)),panelH=std::min(650,std::max(180,height-24));
    SDL_Rect panel{(width-panelW)/2,(height-panelH)/2,panelW,panelH};
    SDL_SetRenderDrawColor(r,18,27,42,250);SDL_RenderFillRect(r,&panel);
    SDL_SetRenderDrawColor(r,97,157,190,255);SDL_RenderDrawRect(r,&panel);
    int pad=std::max(10,panelW/32),x=panel.x+pad,y=panel.y+pad;
    int font=std::clamp(panelW/27,13,23),rowH=std::clamp(panelH/14,25,42);
    std::string title=page==Page::Settings?"SETTINGS":page==Page::Update?"APP UPDATE":"CLOUD SAVE - GOOGLE DRIVE";
    text(r,title,float(x),float(y),float(font+3),{241,205,115,255});
    y+=font+16;
    std::string info;
    if(page==Page::Settings) info="Heroes Lore 5 | Version "+std::string(services::Version);
    else if(page==Page::ConfirmRestore) info="This replaces local saved progress with the cloud backup. A local safety copy is kept. Close and reopen the game afterward.";
    else if(page==Page::ConfirmUpload) info="Upload progress already saved in the game. The previous cloud backup will be replaced. Save your game first.";
    else if(page==Page::ConfirmLogout) info="Remove the Google login from this device. The backup in Drive is kept.";
    else if(page==Page::Cloud) {
        info=s.connected?"Google Drive connected. Save in the game before uploading.":"Continue your Heroes Lore 5 progress on another platform using the same Google account.";
        if(s.backupExists) info+="\nCloud: "+s.backupDate+"\n"+s.backupSummary;
    } else info="Installed: "+std::string(services::Version)+(s.updateAvailable?" | Available: "+s.release.tag:"")+"\nUpdates preserve your saved progress and Google login.";
    y+=wrapped(r,info,x,y,panelW-pad*2,font-1,4)+10;
    if(!s.userCode.empty()) {
        y+=wrapped(r,s.verificationUrl,x,y,panelW-pad*2,font,2)+5;
        text(r,s.userCode,float(x),float(y),float(font+10),{115,214,255,255});y+=font+25;
    }
    auto labels=rows(s);selected=std::clamp(selected,0,int(labels.size())-1);
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
    wrapped(r,s.message,x,y,panelW-pad*2,std::max(12,font-2),std::max(1,(panel.y+panel.h-26-y)/(font+3)));
    text(r,"Arrows / D-pad: move     Confirm: select     Back: close",float(x),float(panel.y+panel.h-25),float(std::max(10,font-6)),{150,175,198,255});
}
}

void notifyPortSettings() {serviceui::notifyGameSettings();}
