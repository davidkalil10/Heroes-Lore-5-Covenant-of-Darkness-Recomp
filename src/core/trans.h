#pragma once
#include "guest.h"
#include "../localization.h"
#include <string>

namespace Trans {
    void init(const std::string& customPath = "");
    u32 translatePtr(u32 strPtr, int maxLen = -1, Locale::UiText context = Locale::UiText::Full);
}
