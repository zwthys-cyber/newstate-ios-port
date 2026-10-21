#include "RemoteImage.hpp"
#include <cstring>
#include <cstdio>

namespace nsport {
bool targetImage(const Memory &memory,uint64_t address,ImageDiagnostics *diagnostics) {
    uint32_t header[8]{};
    if (!memory.bytes(address,header,sizeof header)) {
        if (diagnostics) ++diagnostics->unreadableHeaders;
        return false;
    }
    if (header[0]!=0xfeedfacf || header[1]!=0x100000c || header[3]!=2) return false;
    if (diagnostics) ++diagnostics->executableHeaders;
    auto malformed=[&]() {if (diagnostics) ++diagnostics->malformedCommands;return false;};
    if (!header[4] || header[4]>4096 || header[5]<8 || header[5]>65536) return malformed();
    std::vector<uint8_t> commands(header[5]);
    if (!memory.bytes(address+32,commands.data(),commands.size())) {
        if (diagnostics) ++diagnostics->commandReadFailures;
        return false;
    }
    const uint8_t expected[]={0xaf,0x49,0x43,0x2e,0x9f,0x7f,0x3d,0x7c,
        0x94,0x70,0x8c,0xf9,0x0e,0x3b,0x33,0x78};
    size_t offset=0;bool matched=false,seenUUID=false;std::string actualUUID;
    for (uint32_t i=0;i<header[4];++i) {
        if (offset>commands.size() || commands.size()-offset<8) return malformed();
        uint32_t cmd=0,size=0;
        std::memcpy(&cmd,commands.data()+offset,4);
        std::memcpy(&size,commands.data()+offset+4,4);
        if (size<8 || size>commands.size()-offset) return malformed();
        if (cmd==0x1b) {
            if (size<24 || seenUUID) return malformed();
            seenUUID=true;
            matched=std::memcmp(commands.data()+offset+8,expected,16)==0;
            if (diagnostics) {
                const uint8_t *u=commands.data()+offset+8;
                char text[37];
                std::snprintf(text,sizeof text,"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                    u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],u[11],u[12],u[13],u[14],u[15]);
                actualUUID=text;
            }
        }
        offset+=size;
    }
    if (offset!=commands.size() || !seenUUID) return malformed();
    if (diagnostics && diagnostics->executableUUIDs.size()<8) diagnostics->executableUUIDs.push_back(actualUUID);
    return matched;
}
uint64_t findTargetImage(const Memory &memory,uint64_t allImageInfos,ImageDiagnostics *diagnostics) {
    ImageDiagnostics local;
    ImageDiagnostics &d=diagnostics ? *diagnostics : local;d=ImageDiagnostics{};
    struct Info {uint32_t version,count;uint64_t array;} info{};
    static_assert(sizeof(Info)==16);
    d.stage="dyld_header_unreadable";
    if (!memory.get(allImageInfos,info)) return 0;
    d.version=info.version;d.count=info.count;d.array=info.array;
    if (!info.version || info.count>2048) {d.stage="dyld_header_invalid";return 0;}
    if (!info.count || !info.array) {d.stage="dyld_list_updating_or_empty";return 0;}
    uint32_t count=info.count;
    std::vector<uint64_t> images(static_cast<size_t>(count)*3);
    d.stage="dyld_array_unreadable";
    if (!memory.bytes(info.array,images.data(),images.size()*sizeof(uint64_t))) return 0;
    for (uint32_t i=0;i<count;++i) {
        uint64_t address=images[static_cast<size_t>(i)*3];
        ++d.scanned;
        if (address && targetImage(memory,address,&d)) {
            Info after{};
            if (!memory.get(allImageInfos,after)) {d.stage="dyld_recheck_unreadable";return 0;}
            if (after.array!=info.array || after.count!=info.count) {d.stage="dyld_list_changed";return 0;}
            d.stage="matched";
            return address;
        }
    }
    d.stage=!d.executableUUIDs.empty() ? "executable_uuid_mismatch" :
        d.commandReadFailures ? "executable_commands_unreadable" :
        d.malformedCommands ? "executable_commands_invalid" : "executable_not_found";
    return 0;
}
}
