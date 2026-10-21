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
struct Camera { Vec3 position, rotation; float fov{}; };
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
float distance(Vec3 a, Vec3 b);
bool isA(const Memory &memory, uint64_t objectClass, uint64_t wantedClass);
uint64_t resolveWeak(const Memory &memory, uint64_t address);

enum class Kind { character = 1, vehicle = 2, item = 3 };
struct Marker {
    uint64_t object{};
    Kind kind{};
    Vec3 position;
    float health{}, maximum{}, halfHeight{};
    bool hasHealth{};
    std::u16string playerName;
};
struct Snapshot {
    std::string status;
    uint64_t world{}, controller{}, pawn{}, cameraObject{};
    Camera camera;
    bool cameraReady{};
    uint32_t scanned{}, total{};
    std::vector<Marker> markers;
};

// Memory observations only: no engine function calls, code hooks or writes.
class Sampler {
    struct Candidate { uint64_t object, item; int32_t serial; Kind kind; };
    Memory memory;
    std::unordered_map<uint64_t, int> classKinds;
    std::vector<Candidate> candidates;
    uint64_t lastWorld{};
    uint32_t cursor{};
    int classify(uint64_t objectClass);
    bool inWorld(uint64_t actor, uint64_t world) const;
    bool position(uint64_t actor, Vec3 &out) const;
    void discover(Snapshot &out);
public:
    explicit Sampler(Memory source) : memory(source) {}
    Snapshot sample();
};
} // namespace nsport
