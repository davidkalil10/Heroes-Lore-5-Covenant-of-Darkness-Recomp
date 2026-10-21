#pragma once
#include "hle/hle.h"
namespace native_settings {
bool hook(Cpu&,u32 pc);
void beginFrame();
void endFrame(Screen&);
}
