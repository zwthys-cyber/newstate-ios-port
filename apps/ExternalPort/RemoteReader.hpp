#pragma once
#include "PortCore.hpp"
#include <memory>
#include <chrono>

namespace nsport {
struct RemoteResult {
    std::shared_ptr<Snapshot> snapshot;
    std::string status;
    int pid{},error{};
    bool readPort{};
};
class RemoteReader {
    uint32_t port{};
    int pid{};
    uint64_t base{};
    std::unique_ptr<Sampler> sampler;
    std::chrono::steady_clock::time_point retryAfter{};
    RemoteResult lastFailure;
    static bool read(void *,uint64_t,void *,size_t);
    void reset();
public:
    ~RemoteReader();
    RemoteResult sample(bool skeleton);
};
}
