#import <Foundation/Foundation.h>
#include "RemoteReader.hpp"
#include "RemoteImage.hpp"
#include <mach/mach.h>
#include <mach/task_info.h>
#include <dlfcn.h>
#include <cerrno>
#include <cstring>

namespace nsport {
// Resolve the read-flavor API only. Never fall back to task_for_pid/control ports.
using ReadPortFn=int (*)(mach_port_name_t,int,mach_port_name_t *);
using ListPidsFn=int (*)(void *,int);
using PidPathFn=int (*)(int,void *,uint32_t);
static ReadPortFn readPortAPI() {
    return reinterpret_cast<ReadPortFn>(dlsym(RTLD_DEFAULT,"task_read_for_pid"));
}
static PidPathFn pidPathAPI() {
    return reinterpret_cast<PidPathFn>(dlsym(RTLD_DEFAULT,"proc_pidpath"));
}
static int gamePID(std::string &status) {
    auto list=reinterpret_cast<ListPidsFn>(dlsym(RTLD_DEFAULT,"proc_listallpids"));
    auto path=pidPathAPI();
    if (!list || !path) {status="进程枚举接口不可用";return 0;}
    std::vector<int> pids(8192);
    int count=list(pids.data(),static_cast<int>(pids.size()*sizeof(int)));
    if (count<=0) {status="进程枚举被拒绝或没有返回数据";return 0;}
    if (count>static_cast<int>(pids.size())) count=static_cast<int>(pids.size());
    for (int i=0;i<count;++i) {
        char buffer[4096]{};
        if (pids[i]<=0 || path(pids[i],buffer,sizeof(buffer))<=0) continue;
        buffer[sizeof(buffer)-1]=0;
        const char *leaf=std::strrchr(buffer,'/');
        if (!leaf || std::strcmp(leaf+1,"ExtremeGame")) continue;
        NSString *executable=[NSString stringWithUTF8String:buffer];
        NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:
            [[executable stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"Info.plist"]];
        if (![info[@"CFBundleIdentifier"] isEqualToString:@"com.pubg.newstate"]) continue;
        if (![info[@"CFBundleShortVersionString"] isEqualToString:@"0.9.98"] ||
            ![info[@"CFBundleVersion"] isEqualToString:@"804"]) {
            status="游戏版本不匹配：仅适配 0.9.98（804）";return 0;
        }
        return pids[i];
    }
    status="等待 NEW STATE 进程，请启动原版游戏";return 0;
}
bool RemoteReader::read(void *context,uint64_t address,void *buffer,size_t size) {
    auto *self=static_cast<RemoteReader *>(context);
    if (!self->port) return false;
    vm_size_t copied=0;
    kern_return_t kr=vm_read_overwrite(self->port,static_cast<vm_address_t>(address),static_cast<vm_size_t>(size),
        reinterpret_cast<vm_address_t>(buffer),&copied);
    if (kr!=KERN_SUCCESS || copied!=size) {
        self->memoryError=kr!=KERN_SUCCESS ? kr : KERN_FAILURE;
        self->failedAddress=address;self->failedSize=size;self->copiedSize=copied;
        return false;
    }
    return true;
}
void RemoteReader::reset() {
    sampler.reset();base=0;pid=0;nextDetails={};
    if (port) {mach_port_deallocate(mach_task_self(),port);port=0;}
}
RemoteReader::~RemoteReader() {reset();}
RemoteResult RemoteReader::sample(const SampleOptions &requested) {
    auto now=std::chrono::steady_clock::now();
    if (now<retryAfter) return lastFailure;
    RemoteResult result;
    memoryError=0;failedAddress=failedSize=copiedSize=0;
    int found=0;
    if (pid>0 && port) {
        auto path=pidPathAPI();char buffer[4096]{};
        if (path && path(pid,buffer,sizeof(buffer))>0) {
            buffer[sizeof(buffer)-1]=0;const char *leaf=std::strrchr(buffer,'/');
            if (leaf && !std::strcmp(leaf+1,"ExtremeGame")) found=pid;
        }
    }
    if (!found) found=gamePID(result.status);
    if (!found) {reset();lastFailure=result;retryAfter=now+std::chrono::seconds(1);return result;}
    result.pid=found;
    if (pid!=found) reset();
    if (!port) {
        auto api=readPortAPI();
        if (!api) {result.status="系统未导出 task_read_for_pid，只读访问不可用";lastFailure=result;retryAfter=now+std::chrono::seconds(5);return result;}
        mach_port_name_t acquired=MACH_PORT_NULL;
        errno=0;int rc=api(mach_task_self(),found,&acquired);int saved=errno;
        if (rc || !MACH_PORT_VALID(acquired)) {
            if (MACH_PORT_VALID(acquired)) mach_port_deallocate(mach_task_self(),acquired);
            result.error=saved ? saved : rc;
            result.status="只读任务端口被拒绝（保持开发者模式关闭；不尝试控制端口）";
            lastFailure=result;retryAfter=now+std::chrono::seconds(5);return result;
        }
        port=acquired;pid=found;
    }
    result.readPort=true;
    if (!base) {
        task_dyld_info_data_t info{};
        mach_msg_type_number_t count=TASK_DYLD_INFO_COUNT;
        kern_return_t kr=task_info(port,TASK_DYLD_INFO,reinterpret_cast<task_info_t>(&info),&count);
        if (kr!=KERN_SUCCESS) {
            result.error=kr;result.status="任务端口已取得，但远程镜像信息不可读取";reset();return result;
        }
        Memory memory{this,read,0};
        result.dyldInfoAddress=info.all_image_info_addr;result.dyldInfoSize=info.all_image_info_size;
        result.dyldInfoFormat=info.all_image_info_format;
        if (info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_64) {
            result.images.stage="dyld_format_unsupported";result.status="目标镜像信息不是预期的 64 位格式";reset();return result;
        }
        base=findTargetImage(memory,info.all_image_info_addr,&result.images);
        if (!base) {
            result.error=memoryError;result.failedReadAddress=failedAddress;
            result.failedReadSize=failedSize;result.copiedReadSize=copiedSize;
            result.status="主程序定位失败："+result.images.stage;reset();return result;
        }
        sampler=std::make_unique<Sampler>(Memory{this,read,base});
    }
    Memory memory{this,read,base};
    if (!targetImage(memory,base)) {
        result.status="目标进程已退出或镜像不可读，等待重新连接";reset();return result;
    }
    SampleOptions options=requested;
    options.details=now>=nextDetails;
    options.discoveryBudget=512;
    if (options.details) nextDetails=now+std::chrono::milliseconds(250);
    result.detailsRefreshed=options.details;
    result.snapshot=std::make_shared<Snapshot>(sampler->sample(options));
    result.status=result.snapshot->status;
    return result;
}
}
