// An explicit footer row inside the original Options list. Keep original option
// counts/data intact: the new action must never index the guest's value arrays.
#include "native_settings.h"
#include "service_ui.h"
#include "localization.h"
#include <algorithm>
#include <cstdio>
namespace native_settings {
static u32 state=0,hiddenCursor=0;
static u8 savedCursor=0,savedIndex=0;
static bool title=false,seen=false,chosen=false;
static u8 menuId=0;
static int width=240,height=400;
static int cursorOffset(){return title?14:15;}
static int lastIndex(){return title?8:7;}
static void restoreCursor(){if(hiddenCursor){gm::w8(hiddenCursor,savedCursor);gm::w8(hiddenCursor+1,savedIndex);hiddenCursor=0;}}
static bool options(u32 s,bool t){return s && gm::r8(s+13)==(t?0:1);}
static void remember(u32 s,bool t){if(state!=s || title!=t)chosen=false;state=s;title=t;menuId=gm::r8(s+12);}
bool hook(Cpu& c,u32 pc) {
    u32 expected=pc==0x21eb3c?0xe92d41f0:pc==0x200764?0xe92d47f0:0xe92d4ff0;
    if(gm::r32(pc)!=expected)return false;
    bool t=pc==0x21a1ec || pc==0x21eb3c || pc==0x2186e8;
    if(pc==0x21a1ec || pc==0x1e7d38) {
        if(!options(c.r[0],t)){chosen=false;seen=false;return false;}
        remember(c.r[0],t);seen=true;
        if(chosen){hiddenCursor=state+cursorOffset();savedCursor=gm::r8(hiddenCursor);savedIndex=gm::r8(hiddenCursor+1);gm::w8(hiddenCursor,255);gm::w8(hiddenCursor+1,savedIndex-savedCursor-1);}
        return false;
    }
    restoreCursor();
    if(!options(c.r[0],t))return false;
    remember(c.r[0],t);
    if(pc==0x2186e8 || pc==0x1face8) {
        int x=int(c.r[1]),y=int(c.r[2]);
        if(x>=width/2-76 && x<width/2+62 && y>=height/2+78 && y<height/2+97) {
            chosen=true;serviceui::open();c.r[15]=c.r[14];return true;
        }
        chosen=false;return false;
    }
    int key=int(c.r[1]);
    // Numeric keypad navigation is accepted by the original game too.
    if(key=='2')key=WK_UP;else if(key=='8')key=WK_DOWN;else if(key=='5')key=WK_SELECT;
    int offset=cursorOffset(),index=gm::r8(state+offset+1);
    if(chosen) {
        if(key==WK_SELECT){serviceui::open();}
        else if(key==WK_UP || key==WK_DOWN){
            chosen=false;gm::w8(state+offset,key==WK_UP?5:0);
            gm::w8(state+offset+1,key==WK_UP?lastIndex():0);
        } else if(key==WK_CLEAR){chosen=false;return false;}
        c.r[15]=c.r[14];return true;
    }
    if((key==WK_UP && index==0) || (key==WK_DOWN && index==lastIndex())) {
        chosen=true;c.r[15]=c.r[14];return true;
    }
    return false;
}
void beginFrame(){restoreCursor();}
void endFrame(Screen& screen) {
    restoreCursor();width=screen.w;height=screen.h;
    if(state && (gm::r8(state+12)!=menuId || !options(state,title)))seen=false;
    if(state && gm::r32(0x142838)==0xe92d47f0) {
        u32 common=gm::r32(0x142850+gm::r32(0x1428b4)+gm::r32(0x1428b8));
        if(gm::r32(common+0x244)!=state)seen=false;
    }
    if(!seen){chosen=false;return;}
    static std::string previous;auto trace=std::string(title?"title:":"game:")+Locale::language()+":"+(chosen?"selected":"idle");
    if(SDL_getenv("HL5_TRACE_UI") && trace!=previous){fprintf(stderr,"[native-settings] %s text=%s\n",trace.c_str(),Locale::text("Port settings").c_str());previous=trace;}
    int x=width/2-76,y=height/2+78,w=138,h=19;
    auto put=[&](int px,int py,u32 color){if(px>=0 && py>=0 && px<width && py<height)screen.px[py*width+px]=color;};
    for(int j=0;j<h;j++)for(int i=0;i<w;i++)put(x+i,y+j,chosen?(j<h/2?0xffef8b08:0xffd06b00):(j<h/2?0xffd9dddd:0xffb9c1c4));
    auto label=Locale::text("Port settings");int pen=x+8;
    for(size_t i=0;i<label.size();){
        unsigned char a=label[i++];u32 cp=a;
        if((a&0xe0)==0xc0 && i<label.size())cp=((a&31)<<6)|(label[i++]&63);
        auto glyph=Locale::glyph(cp,11);
        if(glyph)for(int gy=0;gy<glyph->height;gy++)for(int gx=0;gx<glyph->width;gx++) {
            unsigned alpha=glyph->alpha[gy*glyph->width+gx];
            int px=pen+glyph->xoff+gx,py=y+3+glyph->ascent+glyph->yoff+gy;
            if(alpha>100 && px<x+w-4)put(px,py,chosen?0xffffffff:0xff46545e);
        }
        pen+=7;
    }
}
}
