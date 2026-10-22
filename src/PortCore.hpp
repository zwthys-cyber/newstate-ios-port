#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nsport {
struct Vec3 { float x{}, y{}, z{}; };
struct Point { float x{}, y{}; };
struct EdgeIndicator {Point position,direction;bool behind{};};
struct Camera { Vec3 position, rotation; float fov{}; };
struct BoneLine {Vec3 from,to;};
using Read = bool (*)(void *, uint64_t, void *, size_t);
struct Memory {
    void *context{};
    Read read{};
    uint64_t base{};
    bool bytes(uint64_t address, void *out, size_t size) const;
    template <class T> bool get(uint64_t address, T &out) const { return bytes(address, &out, sizeof out); }
    uint64_t pointer(uint64_t address) const;
    uint64_t encodedPointer(uint64_t address) const;
};
uint64_t decodePointer(const std::array<uint64_t, 3> &words);
uint32_t decodeIndex(const std::array<uint32_t, 3> &words);
uint32_t decodeSerial(const std::array<uint32_t, 3> &words);
bool project(Vec3 world, const Camera &camera, float width, float height, Point &out);
bool offscreenIndicator(Vec3 world,const Camera &camera,float width,float height,float inset,EdgeIndicator &out);
// Heading-up horizontal radar: result is a unit-disc offset (right, down).
bool radarOffset(Vec3 world,const Camera &camera,float rangeMeters,Point &out);
bool healthFraction(float current,float maximum,float &fraction);
float distance(Vec3 a, Vec3 b);
bool isA(const Memory &memory, uint64_t objectClass, uint64_t wantedClass);
uint64_t resolveWeak(const Memory &memory, uint64_t address);
std::vector<BoneLine> readSkeleton(const Memory &memory,uint64_t mesh,Vec3 reference);

enum class Kind { character = 1, vehicle = 2, item = 3, airdrop = 4 };
enum class PlayerKind { unknown, human, bot };
enum class Relation { unknown, teammate, enemy };
struct Marker {
    uint64_t object{},playerState{};
    Kind kind{};
    PlayerKind playerKind{};
    Relation relation{};
    Vec3 position;
    float health{}, maximum{}, halfHeight{};
    bool hasHealth{};
    std::u16string playerName;
    std::vector<BoneLine> bones;
};
struct Snapshot {
    std::string status;
    uint64_t world{}, controller{}, pawn{}, cameraObject{};
    Camera camera;
    bool cameraReady{};
    uint32_t scanned{}, total{};
    bool localTeamKnown{},playerArrayAvailable{};
    uint32_t playersChecked{},playersDiscovered{},teammatesHidden{},deadHidden{},stateUnreadable{};
    std::vector<Marker> markers;
};
struct SampleOptions {
    bool skeletons{},details{true},hideTeammates{true};
    uint32_t discoveryBudget{2048};
    uint32_t playerDiscoveryBudget{32};
    uint8_t kinds{7}; // character=1, vehicle=2, item=4; airdrop=8 is explicitly enabled by the external UI
};

// Memory observations only: no engine function calls, code hooks or writes.
class Sampler {
    struct Candidate { uint64_t object, item; int32_t serial; Kind kind; float distance; Marker details;bool detailsReady{}; };
    Memory memory;
    std::unordered_map<uint64_t, int> classKinds;
    std::vector<Candidate> candidates;
    uint64_t lastWorld{};
    uint32_t cursor{},playerCursor{};
    int classify(uint64_t objectClass);
    bool inWorld(uint64_t actor, uint64_t world) const;
    bool position(uint64_t actor, Vec3 &out) const;
    void discoverPlayers(Snapshot &out,const SampleOptions &options);
    void admit(Snapshot &out,const SampleOptions &options,uint64_t object,uint64_t item,int32_t serial,Kind kind);
    void discover(Snapshot &out,const SampleOptions &options);
public:
    explicit Sampler(Memory source) : memory(source) {}
    Snapshot sample(bool includeSkeleton=false);
    Snapshot sample(const SampleOptions &options);
};
} // namespace nsport
