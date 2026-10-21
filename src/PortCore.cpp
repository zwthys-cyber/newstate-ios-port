#include "PortCore.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace nsport {
namespace {
constexpr uint64_t pointerKey = 0xd2ca397cea681802ULL;
constexpr uint64_t objectChunks = 0x8174520, objectCount = 0x8174538;
constexpr uint32_t maximumObjects = 2000000;
constexpr float pi = 3.14159265358979323846f;
uint32_t ror32(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
uint64_t ror64(uint64_t v) { return (v >> 1) | (v << 63); }
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
uint64_t objectClass(const Memory &m, uint64_t object) { return m.encodedPointer(object + 0x30); }
bool objectIsA(const Memory &m, uint64_t object, uint64_t singleton) {
    return object && isA(m, objectClass(m, object), m.pointer(m.base + singleton));
}
struct Item { uint64_t reserved, object; uint32_t flags; int32_t serial; };
static_assert(sizeof(Item) == 24);
struct Array { uint64_t data; int32_t count, capacity; };
bool arrayValid(Array a, int32_t limit) {
    return a.count >= 0 && a.count <= limit && a.capacity >= a.count && a.capacity <= limit;
}
std::u16string playerName(const Memory &m, uint64_t actor) {
    uint64_t state=m.encodedPointer(actor+0x2c0);
    if (!objectIsA(m,state,0x80adb20)) return {};
    // Native GetPlayerName dispatches to a custom virtual path when bit 7 is set.
    uint8_t flags{};
    if (!m.get(state+0x2b8,flags) || (flags&0x80)) return {};
    Array text{};
    if (!m.get(state+0x358,text) || !arrayValid(text,256) || text.count<2) return {};
    std::u16string value(size_t(text.count),u'\0');
    if (!m.bytes(text.data,value.data(),value.size()*sizeof(char16_t)) || value.back()!=0) return {};
    value.pop_back();
    if (value.find(u'\0')!=std::u16string::npos) return {};
    return value;
}
} // namespace

bool Memory::bytes(uint64_t address, void *out, size_t size) const {
    if (!read || !out || !size || size > 2 * 1024 * 1024 || address < 0x10000 ||
        address > 0x0000ffffffffffffULL - size) return false;
    return read(context, address, out, size);
}
uint64_t Memory::pointer(uint64_t address) const {
    uint64_t value{};
    return get(address, value) ? value : 0;
}
uint64_t Memory::encodedPointer(uint64_t address) const {
    std::array<uint64_t, 3> value{};
    return get(address, value) ? decodePointer(value) : 0;
}
uint64_t decodePointer(const std::array<uint64_t, 3> &v) { return v[0] ^ ror64(v[1] ^ v[2] ^ pointerKey); }
uint32_t decodeIndex(const std::array<uint32_t, 3> &v) { return v[0] ^ ror32(v[1] ^ v[2] ^ 0xb9702b7dU, 6); }
uint32_t decodeSerial(const std::array<uint32_t, 3> &v) { return v[0] ^ ror32(v[1] ^ v[2] ^ 0xb5c76d21U, 7); }
bool isA(const Memory &m, uint64_t actual, uint64_t wanted) {
    if (!actual || !wanted) return false;
    if (actual == wanted) return true;
    int32_t index{}, count{};
    if (!m.get(wanted + 0x60, index) || !m.get(actual + 0x60, count) || index < 0 ||
        index > 256 || count < index || count > 256) return false;
    uint64_t chain = m.pointer(actual + 0x58);
    return chain && m.pointer(chain + uint64_t(index) * 8) == wanted + 0x58;
}
uint64_t resolveWeak(const Memory &m, uint64_t address) {
    std::array<uint32_t, 6> words{};
    int32_t count{};
    if (!m.get(address, words) || !m.get(m.base + objectCount, count) ||
        count < 0 || uint32_t(count) > maximumObjects) return 0;
    uint32_t index = decodeIndex({words[0], words[1], words[2]});
    uint32_t serial = decodeSerial({words[3], words[4], words[5]});
    if (!serial || index >= uint32_t(count)) return 0;
    uint64_t table = m.pointer(m.base + objectChunks);
    if (!table) return 0;
    uint64_t chunk = m.pointer(table + uint64_t(index >> 16) * 8);
    Item item{};
    if (!chunk || !m.get(chunk + uint64_t(index & 0xffff) * sizeof(Item), item) ||
        uint32_t(item.serial) != serial || (item.flags & 0x30000000)) return 0;
    return item.object;
}
std::vector<BoneLine> readSkeleton(const Memory &m,uint64_t mesh,Vec3 reference) {
    struct Bone {uint32_t name,number;int32_t parent;};
    struct Transform {float q[4];Vec3 translation,scale;float padding[2];};
    static_assert(sizeof(Bone)==12 && sizeof(Transform)==48 && offsetof(Transform,scale)==28);
    if (!mesh || !objectIsA(m,mesh,0x80b2008)) return {};
    uint8_t registered{};
    if (!m.get(mesh+0x84,registered) || !(registered&1)) return {};
    uint64_t asset=m.pointer(mesh+0x600);Array boneArray{};
    if (!asset || !m.get(asset+0x1b0,boneArray) || !arrayValid(boneArray,512) || boneArray.count<2) return {};
    std::vector<Bone> info(size_t(boneArray.count));
    if (!m.bytes(boneArray.data,info.data(),info.size()*sizeof(Bone))) return {};
    uint64_t poseOwner=resolveWeak(m,mesh+0x5e8);
    std::vector<int32_t> remap;
    if (poseOwner) {
        if (!objectIsA(m,poseOwner,0x80b2008) || !m.get(poseOwner+0x84,registered) || !(registered&1)) return {};
        Array map{};
        if (!m.get(mesh+0x710,map) || !arrayValid(map,2048) || map.count<boneArray.count) return {};
        remap.resize(size_t(map.count));
        if (!m.bytes(map.data,remap.data(),remap.size()*sizeof(int32_t))) return {};
    } else poseOwner=mesh;
    int32_t bufferIndex{};
    if (!m.get(poseOwner+0x6e8,bufferIndex) || bufferIndex<0 || bufferIndex>1) return {};
    uint64_t header=poseOwner+0x678+uint64_t(bufferIndex)*16;Array poses{};
    if (!m.get(header,poses) || !arrayValid(poses,2048) || poses.count<1) return {};
    if (remap.empty() && poses.count<boneArray.count) return {};
    std::vector<Transform> transforms(size_t(poses.count));Transform world{};
    if (!m.get(mesh+0x140,world) || !m.bytes(poses.data,transforms.data(),transforms.size()*sizeof(Transform))) return {};
    Array after{};int32_t afterIndex{};
    if (!m.get(header,after) || !m.get(poseOwner+0x6e8,afterIndex) || afterIndex!=bufferIndex ||
        after.data!=poses.data || after.count!=poses.count || after.capacity!=poses.capacity) return {};
    float norm=0;
    for (float q:world.q) {if (!std::isfinite(q)) return {};norm+=q*q;}
    if (std::abs(norm-1)>0.1f || !finite(world.translation) || !finite(world.scale) ||
        std::abs(world.scale.x)>1000 || std::abs(world.scale.y)>1000 || std::abs(world.scale.z)>1000) return {};
    auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    std::vector<Vec3> points(info.size());std::vector<bool> valid(info.size());
    for (size_t i=0;i<info.size();++i) {
        int32_t index=remap.empty() ? int32_t(i) : remap[i];
        if (index<0 || index>=poses.count) continue;
        Vec3 local=transforms[size_t(index)].translation;
        if (!finite(local)) continue;
        Vec3 scaled{local.x*world.scale.x,local.y*world.scale.y,local.z*world.scale.z};
        Vec3 q{world.q[0],world.q[1],world.q[2]},uv=cross(q,scaled),uuv=cross(q,uv);
        points[i]={world.translation.x+scaled.x+2*(world.q[3]*uv.x+uuv.x),
            world.translation.y+scaled.y+2*(world.q[3]*uv.y+uuv.y),
            world.translation.z+scaled.z+2*(world.q[3]*uv.z+uuv.z)};
        valid[i]=finite(points[i]) && distance(points[i],reference)<1000;
    }
    std::vector<BoneLine> lines;
    for (size_t i=0;i<info.size();++i) {
        int32_t parent=info[i].parent;
        if (parent<0 || parent>=boneArray.count || size_t(parent)==i || !valid[i] || !valid[size_t(parent)]) continue;
        if (distance(points[i],points[size_t(parent)])<300) lines.push_back({points[size_t(parent)],points[i]});
    }
    return lines;
}
float distance(Vec3 a, Vec3 b) {
    return std::hypot(std::hypot(a.x-b.x, a.y-b.y), a.z-b.z);
}
bool project(Vec3 world, const Camera &c, float w, float h, Point &out) {
    if (!finite(world) || !finite(c.position) || !finite(c.rotation) || !std::isfinite(c.fov) ||
        !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0 || c.fov <= 1 || c.fov >= 179) return false;
    float p=c.rotation.x*pi/180, y=c.rotation.y*pi/180, r=c.rotation.z*pi/180;
    float cp=std::cos(p), sp=std::sin(p), cy=std::cos(y), sy=std::sin(y), cr=std::cos(r), sr=std::sin(r);
    Vec3 forward{cp*cy,cp*sy,sp};
    Vec3 right{sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,-sr*cp};
    Vec3 up{-(cr*sp*cy+sr*sy),cy*sr-cr*sp*sy,cr*cp};
    Vec3 d{world.x-c.position.x,world.y-c.position.y,world.z-c.position.z};
    auto dot=[&](Vec3 v){return d.x*v.x+d.y*v.y+d.z*v.z;};
    float depth=dot(forward);
    if (depth <= 1) return false;
    float focal=w*0.5f/std::tan(c.fov*pi/360);
    out={w*0.5f+dot(right)*focal/depth,h*0.5f-dot(up)*focal/depth};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
int Sampler::classify(uint64_t cl) {
    if (!cl) return 0;
    auto found=classKinds.find(cl);
    if (found!=classKinds.end()) return found->second;
    int kind=0;
    // Recovered native UClass singleton locations, relative to the executable image.
    for (const auto &entry : std::array<std::pair<uint64_t,int>,7>{{
        {0x7f2c5b0,1},{0x7f8b4a0,2},{0x7f33a70,2},{0x7f33b00,2},
        {0x7f195c8,2},{0x7f06a00,3},{0x7f06b30,3}}}) {
        uint64_t wanted=memory.pointer(memory.base+entry.first);
        if (wanted && isA(memory,cl,wanted)) { kind=entry.second; break; }
    }
    if (classKinds.size()<8192) classKinds[cl]=kind;
    return kind;
}
bool Sampler::inWorld(uint64_t actor,uint64_t world) const {
    uint64_t outer=memory.encodedPointer(actor+8);
    for (unsigned i=0;outer && i<8;++i) {
        if (objectIsA(memory,outer,0x80a1620)) return memory.pointer(outer+0x178)==world;
        uint64_t next=memory.encodedPointer(outer+8);
        if (next==outer) break;
        outer=next;
    }
    return false;
}
bool Sampler::position(uint64_t actor,Vec3 &out) const {
    uint64_t root=memory.pointer(actor+0x278);
    return root && memory.get(root+0x150,out) && finite(out) &&
        std::abs(out.x)<1e8 && std::abs(out.y)<1e8 && std::abs(out.z)<1e8;
}
void Sampler::discover(Snapshot &out) {
    int32_t count{};
    if (!memory.get(memory.base+objectCount,count) || count<0 || uint32_t(count)>maximumObjects) {
        out.status="对象表尚未就绪"; return;
    }
    out.total=uint32_t(count);
    if (cursor>=out.total) {cursor=0;classKinds.clear();}
    uint64_t table=memory.pointer(memory.base+objectChunks);
    if (!table) return;
    uint32_t work=std::min(2048U,out.total-cursor);
    while (work) {
        uint32_t take=std::min({work,256U,65536U-(cursor&0xffffU)});
        uint64_t chunk=memory.pointer(table+uint64_t(cursor>>16)*8);
        std::array<Item,256> items{};
        uint64_t itemAddress=chunk+uint64_t(cursor&0xffffU)*sizeof(Item);
        if (chunk && memory.bytes(itemAddress,items.data(),take*sizeof(Item))) {
            for (uint32_t i=0;i<take;++i) {
                const auto &it=items[i];
                if (!it.object || !it.serial || (it.flags&0x30000000)) continue;
                int kind=classify(objectClass(memory,it.object));
                if (!kind || it.object==out.pawn || !inWorld(it.object,out.world)) continue;
                bool exists=std::any_of(candidates.begin(),candidates.end(),[&](const Candidate &c){return c.object==it.object;});
                if (exists) continue;
                Vec3 location;
                if (!position(it.object,location)) continue;
                float range=distance(location,out.camera.position);
                if (range>=300000) continue;
                Kind category=Kind(kind);
                size_t limit=category==Kind::vehicle ? 128 : 256;
                size_t countInKind=size_t(std::count_if(candidates.begin(),candidates.end(),[&](const Candidate &c){return c.kind==category;}));
                Candidate incoming{it.object,itemAddress+i*sizeof(Item),it.serial,category,range};
                if (countInKind<limit) candidates.push_back(incoming);
                else {
                    auto farthest=candidates.end();
                    for (auto candidate=candidates.begin();candidate!=candidates.end();++candidate)
                        if (candidate->kind==category && (farthest==candidates.end() || candidate->distance>farthest->distance)) farthest=candidate;
                    if (farthest!=candidates.end() && farthest->distance>range) *farthest=incoming;
                }
            }
        }
        cursor+=take; work-=take;
    }
    out.scanned=cursor;
}
Snapshot Sampler::sample(bool includeSkeleton) {
    Snapshot out;
    out.status="等待游戏初始化";
    uint64_t engine=memory.pointer(memory.base+0x81e8ef0);
    if (!objectIsA(memory,engine,0x8098ac8)) return out;
    uint64_t viewport=memory.pointer(engine+0x960);
    if (!objectIsA(memory,viewport,0x809b590)) return out;
    out.world=memory.pointer(viewport+0x1f8);
    if (!objectIsA(memory,out.world,0x80b74f0)) {out.status="等待游戏世界";return out;}
    if (out.world!=lastWorld) {candidates.clear();classKinds.clear();cursor=0;lastWorld=out.world;}
    Array controllers{};
    if (!memory.get(out.world+0x230,controllers) || !arrayValid(controllers,4096)) return out;
    for (int32_t i=0;i<controllers.count && i<16;++i) {
        uint64_t controller=resolveWeak(memory,controllers.data+uint64_t(i)*24);
        if (objectIsA(memory,controller,0x80ad348)) {out.controller=controller;break;}
    }
    if (!out.controller) {out.status="等待本地控制器";return out;}
    out.pawn=memory.encodedPointer(out.controller+0x370);
    if (!out.pawn) out.pawn=memory.encodedPointer(out.controller+0x2e0);
    out.cameraObject=memory.encodedPointer(out.controller+0x398);
    if (!objectIsA(memory,out.cameraObject,0x80acee8)) {out.status="等待相机";return out;}
    uint64_t vtable=memory.pointer(out.cameraObject);
    if (memory.pointer(vtable+0x6a8)!=memory.base+0x54e7390) {
        out.status="相机读取路径与样本不同，显示已停用";return out;
    }
    auto &c=out.camera;
    if (!memory.get(out.cameraObject+0x15e0,c.position) || !memory.get(out.cameraObject+0x1bac,c.rotation) ||
        !memory.get(out.cameraObject+0x1bc0,c.fov)) return out;
    float overrideFov{};
    if (memory.get(out.cameraObject+0xef4,overrideFov) && overrideFov>0) c.fov=overrideFov;
    if (!finite(c.position) || !finite(c.rotation) || !std::isfinite(c.fov) || c.fov<=1 || c.fov>=179) {
        out.status="相机数据未通过校验";return out;
    }
    out.cameraReady=true; out.status="只读采样中（角色包含队友）";
    discover(out);
    auto it=candidates.begin();
    while (it!=candidates.end()) {
        Item current{};
        if (!memory.get(it->item,current) || current.object!=it->object || current.serial!=it->serial ||
            (current.flags&0x30000000) || !inWorld(it->object,out.world)) {it=candidates.erase(it);continue;}
        Marker marker;marker.object=it->object;marker.kind=it->kind;
        if (position(it->object,marker.position) && distance(marker.position,c.position)<300000) {
            it->distance=distance(marker.position,c.position);
            if (marker.kind==Kind::character) {
                marker.playerName=playerName(memory,marker.object);
                if (includeSkeleton) marker.bones=readSkeleton(memory,memory.encodedPointer(marker.object+0x320),marker.position);
                marker.hasHealth=memory.get(marker.object+0x22f8,marker.health) && memory.get(marker.object+0x6dc,marker.maximum) &&
                    std::isfinite(marker.health) && std::isfinite(marker.maximum) && marker.maximum>0 && marker.maximum<=10000 &&
                    marker.health>=0 && marker.health<=marker.maximum*1.1f;
                uint64_t capsule=memory.encodedPointer(marker.object+0x350);
                if (capsule) memory.get(capsule+0x534,marker.halfHeight);
                if (!std::isfinite(marker.halfHeight) || marker.halfHeight<10 || marker.halfHeight>300) marker.halfHeight=0;
            }
            out.markers.push_back(marker);
        } else {it=candidates.erase(it);continue;}
        ++it;
    }
    return out;
}
} // namespace nsport
