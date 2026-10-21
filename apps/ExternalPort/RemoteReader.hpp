#pragma once
#include "PortCore.hpp"
#include "RemoteImage.hpp"
#include <memory>
#include <chrono>

namespace nsport {
struct RemoteResult {
    std::shared_ptr<Snapshot> snapshot;
    std::string status;
    int pid{},error{};
    bool readPort{};
    bool detailsRefreshed{};
    ImageDiagnostics images;
    uint64_t dyldInfoAddress{},dyldInfoSize{},failedReadAddress{},failedReadSize{},copiedReadSize{};
    uint32_t dyldInfoFormat{};
};
class RemoteReader {
    uint32_t port{};
    int pid{};
    uint64_t base{};
    int memoryError{};
    uint64_t failedAddress{},failedSize{},copiedSize{};
    std::unique_ptr<Sampler> sampler;
    std::chrono::steady_clock::time_point retryAfter{};
    std::chrono::steady_clock::time_point nextDetails{};
    RemoteResult lastFailure;
    static bool read(void *,uint64_t,void *,size_t);
    void reset();
public:
    ~RemoteReader();
    RemoteResult sample(const SampleOptions &options);
};
}
