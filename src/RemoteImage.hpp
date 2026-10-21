#pragma once
#include "PortCore.hpp"

namespace nsport {
struct ImageDiagnostics {
    std::string stage;
    uint32_t version{},count{},scanned{},unreadableHeaders{},executableHeaders{};
    uint32_t commandReadFailures{},malformedCommands{};
    uint64_t array{};
    std::vector<std::string> executableUUIDs;
};
// Remote image validation only; no control port, debugger, injection or writes.
bool targetImage(const Memory &memory,uint64_t address,ImageDiagnostics *diagnostics=nullptr);
uint64_t findTargetImage(const Memory &memory,uint64_t allImageInfos,ImageDiagnostics *diagnostics=nullptr);
}
