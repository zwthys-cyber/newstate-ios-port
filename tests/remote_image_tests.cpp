#include "RemoteImage.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

struct Fixture {
    uint8_t data[8192]{};
    static bool read(void *context,uint64_t address,void *out,size_t size) {
        auto *self=static_cast<Fixture *>(context);
        if (address<0x10000 || address-0x10000>sizeof self->data ||
            size>sizeof self->data-static_cast<size_t>(address-0x10000)) return false;
        std::memcpy(out,self->data+(address-0x10000),size);return true;
    }
    template<class T> void set(size_t off,T value) {std::memcpy(data+off,&value,sizeof value);}
    nsport::Memory memory() {return {this,read,0};}
    Fixture() {
        set<uint32_t>(0,16);set<uint32_t>(4,2);set<uint64_t>(8,0x10100);
        set<uint64_t>(0x100,0x1ffff); // unreadable image must not be dereferenced
        set<uint64_t>(0x118,0x10200);
        set<uint32_t>(0x200,0xfeedfacf);set<uint32_t>(0x204,0x100000c);
        set<uint32_t>(0x20c,2);set<uint32_t>(0x210,1);set<uint32_t>(0x214,24);
        set<uint32_t>(0x220,0x1b);set<uint32_t>(0x224,24);
        const uint8_t uuid[]={0xaf,0x49,0x43,0x2e,0x9f,0x7f,0x3d,0x7c,0x94,0x70,0x8c,0xf9,0x0e,0x3b,0x33,0x78};
        std::memcpy(data+0x228,uuid,16);
    }
};
int main() {
    Fixture f;
    nsport::ImageDiagnostics d;
    assert(nsport::findTargetImage(f.memory(),0x10000,&d)==0x10200);
    assert(d.stage=="matched" && d.count==2 && d.scanned==2 && d.unreadableHeaders==1);
    assert(d.executableUUIDs.size()==1 && d.executableUUIDs[0]=="AF49432E-9F7F-3D7C-9470-8CF90E3B3378");
    f.data[0x228]^=1;assert(!nsport::findTargetImage(f.memory(),0x10000,&d));
    assert(d.stage=="executable_uuid_mismatch" && d.executableUUIDs[0]=="AE49432E-9F7F-3D7C-9470-8CF90E3B3378");
    f.data[0x228]^=1;
    f.set<uint32_t>(0x20c,6);assert(!nsport::targetImage(f.memory(),0x10200));f.set<uint32_t>(0x20c,2);
    f.set<uint32_t>(0x214,0x10001);assert(!nsport::targetImage(f.memory(),0x10200));f.set<uint32_t>(0x214,24);
    f.set<uint32_t>(0x224,7);assert(!nsport::targetImage(f.memory(),0x10200));f.set<uint32_t>(0x224,25);
    assert(!nsport::targetImage(f.memory(),0x10200));f.set<uint32_t>(0x224,24);
    f.set<uint32_t>(4,2049);assert(!nsport::findTargetImage(f.memory(),0x10000));f.set<uint32_t>(4,2);
    f.set<uint64_t>(8,0x1ffff);assert(!nsport::findTargetImage(f.memory(),0x10000,&d));
    assert(d.stage=="dyld_array_unreadable" && d.scanned==0);
    f.set<uint64_t>(8,0);assert(!nsport::findTargetImage(f.memory(),0x10000,&d));
    assert(d.stage=="dyld_list_updating_or_empty");
    assert(!nsport::findTargetImage(f.memory(),0x1ffff,&d));
    assert(d.stage=="dyld_header_unreadable");
    Fixture many;
    many.set<uint32_t>(4,280);many.set<uint64_t>(8,0x10400);
    many.set<uint64_t>(0x400+279*24,0x10200);
    assert(nsport::findTargetImage(many.memory(),0x10000,&d)==0x10200);
    assert(d.scanned==280 && d.stage=="matched");
    std::cout<<"PASS: full bounded dyld list beyond 256 images; exact/mismatched UUID diagnostics; unreadable header/array; updating list; malformed commands\n";
}
