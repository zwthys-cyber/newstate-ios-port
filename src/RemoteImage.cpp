#include "RemoteImage.hpp"
#include <cstring>

namespace nsport {
bool targetImage(const Memory &memory,uint64_t address) {
    uint32_t header[8]{};
    if (!memory.bytes(address,header,sizeof header) || header[0]!=0xfeedfacf ||
        header[1]!=0x100000c || header[3]!=2 || !header[4] || header[4]>4096 ||
        header[5]<8 || header[5]>65536) return false;
    std::vector<uint8_t> commands(header[5]);
    if (!memory.bytes(address+32,commands.data(),commands.size())) return false;
    const uint8_t expected[]={0xaf,0x49,0x43,0x2e,0x9f,0x7f,0x3d,0x7c,
        0x94,0x70,0x8c,0xf9,0x0e,0x3b,0x33,0x78};
    size_t offset=0;bool matched=false,seenUUID=false;
    for (uint32_t i=0;i<header[4];++i) {
        if (offset>commands.size() || commands.size()-offset<8) return false;
        uint32_t cmd=0,size=0;
        std::memcpy(&cmd,commands.data()+offset,4);
        std::memcpy(&size,commands.data()+offset+4,4);
        if (size<8 || size>commands.size()-offset) return false;
        if (cmd==0x1b) {
            if (size<24 || seenUUID) return false;
            seenUUID=true;
            matched=std::memcmp(commands.data()+offset+8,expected,16)==0;
        }
        offset+=size;
    }
    return matched && offset==commands.size();
}
uint64_t findTargetImage(const Memory &memory,uint64_t allImageInfos) {
    struct Info {uint32_t version,count;uint64_t array;} info{};
    static_assert(sizeof(Info)==16);
    if (!memory.get(allImageInfos,info) || !info.version || !info.count ||
        info.count>2048 || !info.array) return 0;
    // Main executable normally comes first. Bound reads if a malformed list is supplied.
    uint32_t count=info.count>256 ? 256 : info.count;
    std::vector<uint64_t> images(static_cast<size_t>(count)*3);
    if (!memory.bytes(info.array,images.data(),images.size()*sizeof(uint64_t))) return 0;
    for (uint32_t i=0;i<count;++i) {
        uint64_t address=images[static_cast<size_t>(i)*3];
        if (address && targetImage(memory,address)) {
            Info after{};
            if (!memory.get(allImageInfos,after) || after.array!=info.array || after.count!=info.count) return 0;
            return address;
        }
    }
    return 0;
}
}
