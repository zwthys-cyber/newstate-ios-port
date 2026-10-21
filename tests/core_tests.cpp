#include "PortCore.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

using namespace nsport;
struct Fake {
    std::map<uint64_t,std::vector<uint8_t>> regions;
    void allocate(uint64_t a,size_t n) {regions[a]=std::vector<uint8_t>(n);}
    static bool read(void *ctx,uint64_t a,void *out,size_t n) {
        Fake &self=*static_cast<Fake *>(ctx);
        auto it=self.regions.upper_bound(a);
        if (it==self.regions.begin()) return false;
        --it;
        uint64_t off=a-it->first;
        if (off>it->second.size() || n>it->second.size()-off) return false;
        std::memcpy(out,it->second.data()+off,n);return true;
    }
    template<class T> void set(uint64_t a,T value) {
        auto it=regions.upper_bound(a);assert(it!=regions.begin());--it;
        uint64_t off=a-it->first;
        assert(off+sizeof(value)<=it->second.size());std::memcpy(it->second.data()+off,&value,sizeof(value));
    }
};
static uint32_t rol32(uint32_t x,unsigned n) {return (x<<n)|(x>>(32-n));}
static std::array<uint32_t,6> weak(uint32_t index,uint32_t serial) {
    return {0x11223344,0xaabbccdd,0xaabbccddU^0xb9702b7dU^rol32(index^0x11223344U,6),
        0x98765432,0x10203040,0x10203040U^0xb5c76d21U^rol32(serial^0x98765432U,7)};
}
static void crypto() {
    assert(decodePointer({0x123456789abcdef0ULL,0x0fedcba987654321ULL,0x1122334455667788ULL})==0xf436b6b0068948a5ULL);
    assert(decodeIndex({0x12345678,0x9abcdef0,0xfedcba98})==0x45401744);
    assert(decodeSerial({0x12345678,0x9abcdef0,0xfedcba98})==0x8197186a);
    // The same XOR+ROR representation must preserve encoded nulls as null.
    assert(decodePointer({0x69651cbe75340c01ULL,0,0})==0);
}
static void projection() {
    Camera camera{{0,0,0},{0,0,0},90};Point p;
    auto near=[](float a,float b){return std::abs(a-b)<0.02f;};
    assert(project({100,0,0},camera,2000,1000,p) && near(p.x,1000) && near(p.y,500));
    assert(project({100,100,0},camera,2000,1000,p) && near(p.x,2000) && near(p.y,500));
    assert(project({100,0,50},camera,2000,1000,p) && near(p.y,0));
    assert(!project({-100,0,0},camera,2000,1000,p));
    camera.rotation={0,90,0};
    assert(project({0,100,0},camera,2000,1000,p) && near(p.x,1000) && near(p.y,500));
    camera.rotation={90,0,0};
    assert(project({0,0,100},camera,2000,1000,p) && near(p.x,1000) && near(p.y,500));
    camera.fov=0;assert(!project({100,0,0},camera,2000,1000,p));
    camera.fov=90;assert(!project({NAN,0,0},camera,2000,1000,p));
    assert(!project({100,0,0},camera,0,1000,p));
    assert(near(distance({0,0,0},{3,4,12}),13));
}
static void objectLifecycle() {
    Fake f;constexpr uint64_t base=0x100000000,table=0x200000000,chunk=0x200010000,address=0x200020000;
    f.allocate(base+0x8174500,0x100);f.allocate(table,16);f.allocate(chunk,128);f.allocate(address,64);
    f.set(base+0x8174520,table);f.set(base+0x8174538,int32_t(2));f.set(table,chunk);
    f.set(address,weak(1,1234));f.set(chunk+24+8,uint64_t(0x300000000));
    f.set(chunk+24+16,uint32_t(0));f.set(chunk+24+20,int32_t(1234));
    Memory m{&f,Fake::read,base};
    assert(resolveWeak(m,address)==0x300000000);
    f.set(chunk+24+20,int32_t(1235));assert(resolveWeak(m,address)==0);
    f.set(chunk+24+20,int32_t(1234));f.set(chunk+24+16,uint32_t(0x20000000));assert(resolveWeak(m,address)==0);
    f.set(chunk+24+16,uint32_t(0));f.set(address,weak(2,1234));assert(resolveWeak(m,address)==0);
    f.set(address,weak(1,0));assert(resolveWeak(m,address)==0);
    f.set(base+0x8174538,int32_t(-1));assert(resolveWeak(m,address)==0);
    f.set(base+0x8174538,int32_t(2000001));assert(resolveWeak(m,address)==0);
    uint64_t buffer=0;assert(!m.bytes(0,&buffer,8));assert(!m.bytes(UINT64_MAX-3,&buffer,8));
    assert(!m.bytes(address,&buffer,3*1024*1024));
}
static void inheritanceAndUnreadableMemory() {
    Fake f;uint64_t parent=0x300000000,child=0x300010000,chain=0x300020000;
    f.allocate(parent,128);f.allocate(child,128);f.allocate(chain,32);
    f.set(parent+0x60,int32_t(1));f.set(child+0x60,int32_t(2));f.set(child+0x58,chain);f.set(chain+8,parent+0x58);
    Memory memory{&f,Fake::read,0x100000000};
    assert(isA(memory,child,parent));assert(isA(memory,parent,parent));
    f.set(chain+8,uint64_t(0));assert(!isA(memory,child,parent));
    f.set(parent+0x60,int32_t(-1));assert(!isA(memory,child,parent));
    f.set(parent+0x60,int32_t(257));assert(!isA(memory,child,parent));
    assert(!isA(memory,0,parent));
    Sampler sampler(memory);auto snapshot=sampler.sample();assert(!snapshot.cameraReady && snapshot.markers.empty());
}
int main() {
    crypto();projection();objectLifecycle();inheritanceAndUnreadableMemory();
    std::cout<<"PASS: decode golden vectors; camera projection and invalid input; weak-object serial/lifecycle/bounds; class hierarchy; unreadable memory\n";
}
