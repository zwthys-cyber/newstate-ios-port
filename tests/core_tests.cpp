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
static std::array<uint64_t,3> encoded(uint64_t pointer) {
    uint64_t mask=0x0123456789abcdefULL^0x8877665544332211ULL^0xd2ca397cea681802ULL;
    mask=(mask>>1)|(mask<<63);
    return {pointer^mask,0x0123456789abcdefULL,0x8877665544332211ULL};
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
static void completeReadPipeline() {
    Fake f;constexpr uint64_t base=0x100000000;
    constexpr uint64_t engine=0x300000000,viewport=0x300010000,world=0x300020000,controller=0x300030000;
    constexpr uint64_t pawn=0x300040000,camera=0x300050000,target=0x300060000,root=0x300070000;
    constexpr uint64_t level=0x300080000,capsule=0x300090000,vtable=0x3000a0000,weakList=0x3000b0000;
    constexpr uint64_t chunk=0x3000c0000,table=0x3000d0000;
    for (uint64_t a:{engine,viewport,world,controller,pawn,camera,target,root,level,capsule,vtable,weakList,chunk,table}) f.allocate(a,0x4000);
    std::array<uint64_t,7> slots{0x8098ac8,0x809b590,0x80b74f0,0x80ad348,0x80acee8,0x7f2c5b0,0x80a1620};
    std::array<uint64_t,7> instances{engine,viewport,world,controller,camera,target,level};
    for (size_t i=0;i<slots.size();++i) {
        uint64_t cl=0x400000000+i*0x1000;f.allocate(cl,0x100);
        f.allocate(base+slots[i],8);f.set(base+slots[i],cl);f.set(instances[i]+0x30,encoded(cl));
    }
    f.allocate(base+0x81e8ef0,8);f.set(base+0x81e8ef0,engine);
    f.allocate(base+0x8174500,0x100);f.set(base+0x8174520,table);f.set(base+0x8174538,int32_t(2));f.set(table,chunk);
    f.set(chunk+8,controller);f.set(chunk+20,int32_t(111));f.set(chunk+24+8,target);f.set(chunk+24+20,int32_t(222));
    f.set(engine+0x960,viewport);f.set(viewport+0x1f8,world);
    f.set(world+0x230,weakList);f.set(world+0x238,int32_t(1));f.set(world+0x23c,int32_t(1));f.set(weakList,weak(0,111));
    f.set(controller+0x370,encoded(pawn));f.set(controller+0x398,encoded(camera));
    f.set(camera,vtable);f.set(vtable+0x6a8,base+0x54e7390);
    f.set(camera+0x15e0,Vec3{0,0,0});f.set(camera+0x1bac,Vec3{0,0,0});f.set(camera+0x1bc0,90.0f);
    f.set(target+8,encoded(level));f.set(level+0x178,world);f.set(target+0x278,root);f.set(root+0x150,Vec3{1000,100,0});
    f.set(target+0x22f8,75.0f);f.set(target+0x6dc,100.0f);f.set(target+0x350,encoded(capsule));f.set(capsule+0x534,88.0f);
    Memory memory{&f,Fake::read,base};Sampler sampler(memory);
    auto out=sampler.sample();
    assert(out.cameraReady && out.world==world && out.controller==controller && out.pawn==pawn);
    assert(out.markers.size()==1 && out.markers[0].kind==Kind::character && out.markers[0].hasHealth);
    assert(out.markers[0].health==75 && out.markers[0].maximum==100 && out.markers[0].halfHeight==88);
    // Objects being destroyed must disappear from previously cached candidates.
    f.set(chunk+24+16,uint32_t(0x20000000));out=sampler.sample();assert(out.cameraReady && out.markers.empty());
    f.set(chunk+24+16,uint32_t(0));f.set(level+0x178,uint64_t(world+0x8000));
    out=sampler.sample();assert(out.markers.empty());
    f.set(level+0x178,world);f.set(vtable+0x6a8,base+0x54e741c);
    out=sampler.sample();assert(!out.cameraReady && out.markers.empty());
    f.set(vtable+0x6a8,base+0x54e7390);f.set(camera+0x1bc0,NAN);
    out=sampler.sample();assert(!out.cameraReady && out.markers.empty());
    f.set(camera+0x1bc0,90.0f);f.set(chunk+20,int32_t(999));
    out=sampler.sample();assert(!out.cameraReady && out.controller==0);
}
int main() {
    crypto();projection();objectLifecycle();inheritanceAndUnreadableMemory();completeReadPipeline();
    std::cout<<"PASS: decode golden vectors; camera projection and invalid input; weak-object serial/lifecycle/bounds; class hierarchy; unreadable memory; complete synthetic world/controller/camera/marker pipeline and invalidation\n";
}
