#include "PortCore.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
#include <functional>

using namespace nsport;
struct Fake {
    std::map<uint64_t,std::vector<uint8_t>> regions;
    size_t reads{};
    std::function<void(uint64_t,size_t)> beforeRead;
    void allocate(uint64_t a,size_t n) {regions[a]=std::vector<uint8_t>(n);}
    static bool read(void *ctx,uint64_t a,void *out,size_t n) {
        Fake &self=*static_cast<Fake *>(ctx);
        ++self.reads;
        if (self.beforeRead) self.beforeRead(a,n);
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
static void radarProjection() {
    Camera c{{1000,2000,300},{0,0,0},90};Point p;
    auto near=[](float a,float b){return std::abs(a-b)<0.0001f;};
    assert(radarOffset({11000,2000,300},c,200,p) && near(p.x,0) && near(p.y,-0.5f));
    assert(radarOffset({1000,12000,300},c,200,p) && near(p.x,0.5f) && near(p.y,0));
    assert(radarOffset({-9000,2000,300},c,200,p) && near(p.y,0.5f));
    c.rotation.y=90;
    assert(radarOffset({1000,12000,300},c,200,p) && near(p.x,0) && near(p.y,-0.5f));
    assert(radarOffset({11000,2000,300},c,200,p) && near(p.x,-0.5f));
    assert(radarOffset({1000,22000,9000},c,200,p) && near(p.y,-1)); // horizontal range, independent of height
    assert(!radarOffset({1000,22001,300},c,200,p));
    assert(radarOffset(c.position,c,200,p) && near(p.x,0) && near(p.y,0));
    assert(!radarOffset({NAN,0,0},c,200,p));
    assert(!radarOffset(c.position,c,0,p));assert(!radarOffset(c.position,c,INFINITY,p));
    c.rotation.y=NAN;assert(!radarOffset(c.position,c,200,p));
}
static void healthAndEdgeIndicators() {
    float fraction=42;
    assert(healthFraction(75,100,fraction) && std::abs(fraction-0.75f)<0.0001f);
    assert(healthFraction(0,100,fraction) && fraction==0);
    assert(healthFraction(110,100,fraction) && fraction==1);
    for (auto invalid:std::array<std::pair<float,float>,6>{{{NAN,100},{10,NAN},{-1,100},{1,0},{1,10001},{111,100}}})
        assert(!healthFraction(invalid.first,invalid.second,fraction) && fraction==0);
    Camera c{{0,0,0},{0,0,0},90};EdgeIndicator e;
    assert(!offscreenIndicator({100,0,0},c,2000,1000,34,e));
    assert(offscreenIndicator({100,200,0},c,2000,1000,34,e) && !e.behind);
    assert(std::abs(e.position.x-1966)<0.01f && std::abs(e.position.y-500)<0.01f && e.direction.x>0);
    assert(offscreenIndicator({100,-200,100},c,2000,1000,34,e));
    assert(e.position.x>=34 && e.position.y>=34 && e.direction.x<0 && e.direction.y<0);
    assert(offscreenIndicator({-100,0,0},c,2000,1000,34,e) && e.behind);
    assert(std::abs(e.position.x-1000)<0.01f && std::abs(e.position.y-966)<0.01f);
    c.rotation.y=90;
    assert(!offscreenIndicator({0,100,0},c,2000,1000,34,e));
    assert(offscreenIndicator({0,-100,0},c,2000,1000,34,e) && e.behind);
    assert(std::abs(e.position.y-966)<0.01f);
    assert(!offscreenIndicator({NAN,0,0},c,2000,1000,34,e));
    assert(!offscreenIndicator({-100,0,0},c,60,60,34,e));
    assert(!offscreenIndicator(c.position,c,2000,1000,34,e));
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
    constexpr uint64_t nameData=0x3000e0000,state=0x3000f0000;
    for (uint64_t a:{engine,viewport,world,controller,pawn,camera,target,root,level,capsule,vtable,weakList,chunk,table}) f.allocate(a,0x4000);
    f.allocate(nameData,512);f.allocate(state,0x400);
    std::array<uint64_t,8> slots{0x8098ac8,0x809b590,0x80b74f0,0x80ad348,0x80acee8,0x7f2c5b0,0x80a1620,0x80adb20};
    std::array<uint64_t,8> instances{engine,viewport,world,controller,camera,target,level,state};
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
    f.set(target+0x2c0,encoded(state));f.set(state+0x358,nameData);f.set(state+0x360,int32_t(6));f.set(state+0x364,int32_t(6));
    f.set(pawn+0x30,encoded(uint64_t(0x400005000)));
    f.set(pawn+0xc04,std::array<int32_t,2>{10,0});f.set(target+0xc04,std::array<int32_t,2>{11,1});
    std::array<char16_t,6> testName{u'测',u'试',u'B',u'o',u't',0};f.set(nameData,testName);
    Memory memory{&f,Fake::read,base};Sampler sampler(memory);
    auto out=sampler.sample();
    assert(out.cameraReady && out.world==world && out.controller==controller && out.pawn==pawn);
    assert(out.markers.size()==1 && out.markers[0].kind==Kind::character && out.markers[0].hasHealth);
    assert(out.markers[0].health==75 && out.markers[0].maximum==100 && out.markers[0].halfHeight==88);
    assert(out.markers[0].playerName==u"测试Bot");
    assert(out.localTeamKnown && out.markers[0].relation==Relation::enemy && out.markers[0].playerKind==PlayerKind::human);
    // Fast samples retain labels but read fresh position/camera and reject destroyed objects.
    size_t slowReads=f.reads;
    SampleOptions fast;fast.details=false;fast.discoveryBudget=0;fast.kinds=1;
    f.set(root+0x150,Vec3{2000,100,0});
    // Rotate only after the initial camera read, while target positions are sampled.
    f.beforeRead=[&](uint64_t address,size_t) {
        if (address==root+0x150) f.set(camera+0x1bac,Vec3{0,90,0});
    };
    size_t before=f.reads;auto moving=sampler.sample(fast);
    assert(moving.cameraReady && moving.markers.size()==1 && moving.markers[0].position.x==2000);
    assert(moving.camera.rotation.y==90 && moving.markers[0].playerName==u"测试Bot");
    assert(f.reads-before<slowReads);
    f.beforeRead={};
    // Health, team affiliation and bot classification must not wait for cached name refresh.
    f.set(target+0x22f8,42.0f);f.set(state+0x2b8,uint8_t(8));moving=sampler.sample(fast);
    assert(moving.markers.size()==1 && moving.markers[0].health==42 && moving.markers[0].playerKind==PlayerKind::bot);
    f.set(target+0xc04,std::array<int32_t,2>{10,1});moving=sampler.sample(fast);
    assert(moving.markers.empty() && moving.teammatesHidden==1);
    fast.hideTeammates=false;moving=sampler.sample(fast);
    assert(moving.markers.size()==1 && moving.markers[0].relation==Relation::teammate);
    fast.hideTeammates=true;f.set(target+0xc04,std::array<int32_t,2>{11,1});
    f.set(pawn+0xc04,std::array<int32_t,2>{-1,-1});moving=sampler.sample(fast);
    assert(!moving.localTeamKnown && moving.markers.size()==1 && moving.markers[0].relation==Relation::unknown);
    f.set(pawn+0xc04,std::array<int32_t,2>{0,0});f.set(target+0xc04,std::array<int32_t,2>{0,1});moving=sampler.sample(fast);
    assert(moving.markers.empty() && moving.teammatesHidden==1); // team zero is valid; -1 is invalid
    f.set(pawn+0xc04,std::array<int32_t,2>{10,0});f.set(target+0xc04,std::array<int32_t,2>{11,1});
    f.set(state+0x30,encoded(0));moving=sampler.sample(fast);
    assert(moving.markers.size()==1 && moving.markers[0].playerKind==PlayerKind::unknown && moving.markers[0].playerName.empty());
    f.set(state+0x30,encoded(uint64_t(0x400007000)));f.set(state+0x2b8,uint8_t(0));
    f.set(target+0x22f8,0.0f);moving=sampler.sample(fast);
    assert(moving.markers.size()==1 && moving.markers[0].health==0); // zero HP can be downed; never infer death from HP alone
    f.set(target+0x22f8,NAN);moving=sampler.sample(fast);assert(moving.markers.size()==1 && !moving.markers[0].hasHealth);
    f.set(target+0x22f8,75.0f);
    auto targetBytes=std::move(f.regions[target]);f.regions.erase(target);moving=sampler.sample(fast);
    assert(moving.markers.empty() && moving.stateUnreadable==1);
    f.regions[target]=std::move(targetBytes);f.set(target+0x930,uint8_t(1));moving=sampler.sample(fast);
    assert(moving.markers.empty() && moving.deadHidden==1);
    out=sampler.sample();assert(out.markers.empty()); // dead object must not be rediscovered as live
    f.set(target+0x930,uint8_t(0));out=sampler.sample();assert(out.markers.size()==1); // same actor can respawn
    f.set(chunk+24+16,uint32_t(0x20000000));moving=sampler.sample(fast);assert(moving.markers.empty());
    f.set(chunk+24+16,uint32_t(0));f.set(root+0x150,Vec3{1000,100,0});f.set(camera+0x1bac,Vec3{0,0,0});
    out=sampler.sample();assert(out.markers.size()==1);
    f.set(state+0x2b8,uint8_t(0x80));out=sampler.sample();assert(out.markers[0].playerName.empty());
    f.set(state+0x2b8,uint8_t(0));f.set(state+0x360,int32_t(300));out=sampler.sample();assert(out.markers[0].playerName.empty());
    f.set(state+0x360,int32_t(6));testName.back()=u'X';f.set(nameData,testName);out=sampler.sample();assert(out.markers[0].playerName.empty());
    testName.back()=0;f.set(nameData,testName);
    // PlayerArray bypasses the object-table discovery cursor, while still checking GUObjectArray identity.
    constexpr uint64_t gameState=0x300100000,gameStateClass=0x400020000,players=0x300110000;
    f.allocate(gameState,0x400);f.allocate(gameStateClass,0x100);f.allocate(players,16);
    f.allocate(base+0x809ae70,8);f.set(base+0x809ae70,gameStateClass);f.set(gameState+0x30,encoded(gameStateClass));
    f.set(world+0x148,gameState);f.set(gameState+0x2c8,players);f.set(gameState+0x2d0,int32_t(1));f.set(gameState+0x2d4,int32_t(1));
    f.set(players,state);f.set(state+0x368,target);
    auto indexWords=weak(0,1);f.set(target+0x20,std::array<uint32_t,3>{indexWords[3],indexWords[4],indexWords[5]});
    SampleOptions direct;direct.discoveryBudget=0;Sampler immediate(memory);auto found=immediate.sample(direct);
    assert(found.markers.size()==1 && found.playerArrayAvailable && found.playersChecked==1 && found.playersDiscovered==1 && found.scanned==0);
    indexWords=weak(0,0);f.set(target+0x20,std::array<uint32_t,3>{indexWords[3],indexWords[4],indexWords[5]});
    Sampler mismatched(memory);found=mismatched.sample(direct);assert(found.markers.empty());
    indexWords=weak(0,1);f.set(target+0x20,std::array<uint32_t,3>{indexWords[3],indexWords[4],indexWords[5]});
    f.set(root+0x150,Vec3{300000,0,0});Sampler far(memory);found=far.sample(direct);assert(found.markers.empty());
    f.set(root+0x150,Vec3{299999,0,0});Sampler within(memory);found=within.sample(direct);assert(found.markers.size()==1);
    f.set(root+0x150,Vec3{1000,100,0});
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
    // Many item objects must not consume the character discovery budget.
    f.set(chunk+20,int32_t(111));f.set(base+0x8174538,int32_t(602));
    constexpr uint64_t itemClass=0x400010000;
    f.allocate(itemClass,0x100);f.allocate(base+0x7f06a00,8);f.set(base+0x7f06a00,itemClass);
    for (uint64_t i=1;i<=600;++i) {
        uint64_t item=0x700000000+i*0x1000;f.allocate(item,0x400);
        f.set(item+0x30,encoded(itemClass));f.set(item+8,encoded(level));f.set(item+0x278,root);
        f.set(chunk+i*24+8,item);f.set(chunk+i*24+16,uint32_t(0));f.set(chunk+i*24+20,int32_t(i));
    }
    f.set(chunk+601*24+8,target);f.set(chunk+601*24+16,uint32_t(0));f.set(chunk+601*24+20,int32_t(222));
    Sampler crowded(memory);out=crowded.sample();
    size_t characters=0,items=0;
    for (const auto &marker:out.markers) {if (marker.kind==Kind::character) ++characters;else if (marker.kind==Kind::item) ++items;}
    assert(characters==1 && items==256);
    // Airdrops are a separate category, including Blueprint subclasses, not ordinary loot.
    constexpr uint64_t dropClass=0x400030000,dropChildClass=0x400031000,dropChain=0x400032000;
    constexpr uint64_t dropRoot=0x300120000;
    for (uint64_t a:{dropClass,dropChildClass,dropChain}) f.allocate(a,0x100);
    f.allocate(dropRoot,0x400);f.set(dropRoot+0x150,Vec3{2000,100,0});
    f.allocate(base+0x7f02930,8);f.set(base+0x7f02930,dropClass);
    f.set(dropChildClass+0x60,int32_t(1));f.set(dropChildClass+0x58,dropChain);f.set(dropChain,dropClass+0x58);
    f.set(base+0x8174538,int32_t(604));
    for (uint64_t i=602;i<604;++i) {
        uint64_t drop=0x700000000+i*0x1000;f.allocate(drop,0x600);
        f.set(drop+0x30,encoded(i==602 ? dropClass : dropChildClass));
        f.set(drop+8,encoded(level));f.set(drop+0x278,dropRoot);
        f.set(chunk+i*24+8,drop);f.set(chunk+i*24+16,uint32_t(0));f.set(chunk+i*24+20,int32_t(i));
    }
    SampleOptions dropsOnly;dropsOnly.kinds=8;Sampler drops(memory);out=drops.sample(dropsOnly);
    assert(out.markers.size()==2);
    for (const auto &marker:out.markers) assert(marker.kind==Kind::airdrop && !marker.hasHealth);
    dropsOnly.kinds=4;out=drops.sample(dropsOnly);assert(out.markers.size()==256);
    for (const auto &marker:out.markers) assert(marker.kind==Kind::item);
    dropsOnly.kinds=8;dropsOnly.details=false;dropsOnly.discoveryBudget=0;
    f.set(dropRoot+0x150,Vec3{2500,100,0});out=drops.sample(dropsOnly);
    assert(out.markers.size()==2 && out.markers[0].position.x==2500);
    f.set(chunk+602*24+16,uint32_t(0x20000000));out=drops.sample(dropsOnly);
    assert(out.markers.size()==1 && out.markers[0].object==0x700000000+603*0x1000);
    f.set(chunk+603*24+20,int32_t(999));out=drops.sample(dropsOnly);assert(out.markers.empty());
    f.set(chunk+602*24+16,uint32_t(0));f.set(chunk+603*24+20,int32_t(603));
    dropsOnly.discoveryBudget=2048;f.set(dropRoot+0x150,Vec3{300000,0,0});
    Sampler farDrops(memory);out=farDrops.sample(dropsOnly);assert(out.markers.empty());
    f.set(dropRoot+0x150,Vec3{2000,100,0});f.set(level+0x178,uint64_t(world+0x8000));
    Sampler otherWorldDrops(memory);out=otherWorldDrops.sample(dropsOnly);assert(out.markers.empty());
}
static void skeletonReader() {
    struct Transform {float q[4];Vec3 translation,scale;float padding[2];};
    struct Bone {uint32_t name,number;int32_t parent;};
    static_assert(sizeof(Transform)==48 && sizeof(Bone)==12);
    Fake f;constexpr uint64_t base=0x100000000,mesh=0x500000000,asset=0x500010000;
    constexpr uint64_t infos=0x500020000,poses=0x500030000,cl=0x600000000;
    for (uint64_t a:{mesh,asset,infos,poses,cl}) f.allocate(a,0x1000);
    f.allocate(base+0x80b2008,8);f.set(base+0x80b2008,cl);f.set(mesh+0x30,encoded(cl));
    f.set(mesh+0x84,uint8_t(1));f.set(mesh+0x600,asset);
    f.set(asset+0x1b0,infos);f.set(asset+0x1b8,int32_t(3));f.set(asset+0x1bc,int32_t(3));
    std::array<Bone,3> bones{{{1,0,-1},{2,0,0},{3,0,1}}};f.set(infos,bones);
    f.set(mesh+0x678,poses);f.set(mesh+0x680,int32_t(3));f.set(mesh+0x684,int32_t(3));
    Transform world{{0,0,0.70710678f,0.70710678f},{100,200,300},{2,1,1},{0,0}};
    f.set(mesh+0x140,world);
    for (int i=0;i<3;++i) {
        Transform bone{{0,0,0,1},{float(10*i),0,0},{1,1,1},{0,0}};
        f.set(poses+uint64_t(i)*48,bone);
    }
    Memory memory{&f,Fake::read,base};auto lines=readSkeleton(memory,mesh,{100,200,300});
    assert(lines.size()==2 && std::abs(lines[0].from.y-200)<0.01f && std::abs(lines[0].to.y-220)<0.01f);
    assert(std::abs(lines[1].to.x-100)<0.01f && std::abs(lines[1].to.y-240)<0.01f);
    f.set(mesh+0x6e8,int32_t(2));assert(readSkeleton(memory,mesh,{100,200,300}).empty());
    f.set(mesh+0x6e8,int32_t(0));f.set(mesh+0x84,uint8_t(0));assert(readSkeleton(memory,mesh,{100,200,300}).empty());
    f.set(mesh+0x84,uint8_t(1));bones[2].parent=999;f.set(infos,bones);
    assert(readSkeleton(memory,mesh,{100,200,300}).size()==1);
    bones[2].parent=1;f.set(infos,bones);
    constexpr uint64_t leader=0x500040000,map=0x500050000,table=0x500060000,chunk=0x500070000;
    for (uint64_t a:{leader,map,table,chunk}) f.allocate(a,0x1000);
    f.allocate(base+0x8174500,0x100);f.set(base+0x8174520,table);f.set(base+0x8174538,int32_t(1));
    f.set(table,chunk);f.set(chunk+8,leader);f.set(chunk+20,int32_t(777));f.set(mesh+0x5e8,weak(0,777));
    f.set(leader+0x30,encoded(cl));f.set(leader+0x84,uint8_t(1));
    f.set(leader+0x678,poses);f.set(leader+0x680,int32_t(3));f.set(leader+0x684,int32_t(3));
    f.set(mesh+0x710,map);f.set(mesh+0x718,int32_t(3));f.set(mesh+0x71c,int32_t(3));
    f.set(map,std::array<int32_t,3>{2,1,0});f.set(mesh+0x678,uint64_t(0));
    lines=readSkeleton(memory,mesh,{100,200,300});
    assert(lines.size()==2 && std::abs(lines[0].from.y-240)<0.01f && std::abs(lines[1].to.y-200)<0.01f);
    f.set(map,std::array<int32_t,3>{2,-1,0});assert(readSkeleton(memory,mesh,{100,200,300}).empty());
}
int main() {
    crypto();projection();radarProjection();healthAndEdgeIndicators();objectLifecycle();inheritanceAndUnreadableMemory();completeReadPipeline();skeletonReader();
    std::cout<<"PASS: decode; projection/edges; heading-up radar/yaw/horizontal range/boundaries/invalid data; fresh health/team/bot/death state without metadata refresh; unknown classification; zero-HP downed actor; unreadable/dead/destructed removal and respawn; guarded PlayerArray discovery without global traversal; strict 3000m range; independent airdrops/subclasses, toggles, fresh position, destroyed/recycled/range/world removal; fresh motion/camera with cached labels and fewer reads; skeleton transforms/buffers/leader-remap\n";
}
