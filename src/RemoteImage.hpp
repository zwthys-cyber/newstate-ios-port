#pragma once
#include "PortCore.hpp"

namespace nsport {
// Remote image validation only; no control port, debugger, injection or writes.
bool targetImage(const Memory &memory,uint64_t address);
uint64_t findTargetImage(const Memory &memory,uint64_t allImageInfos);
}
