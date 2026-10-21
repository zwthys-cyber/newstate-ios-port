// Load-stage diagnosis only: no game reads, hooks, UIKit or security changes.
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char NSPTraceDocuments[1024];
static char NSPTraceTemporary[1024];

static void NSPTraceAppend(const char *path,const char *line,size_t length) {
    if (!path[0]) return;
    int fd=open(path,O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC,0600);
    if (fd<0) return;
    size_t offset=0;
    while (offset<length) {
        ssize_t count=write(fd,line+offset,length-offset);
        if (count<0 && errno==EINTR) continue;
        if (count<=0) break;
        offset+=(size_t)count;
    }
    close(fd);
}

static void NSPTraceRecord(const char *phase) {
    struct timespec now={0};
    (void)clock_gettime(CLOCK_MONOTONIC,&now);
    char line[192];
    int length=snprintf(line,sizeof(line),"StartupTrace 1 pid=%d monotonic=%lld.%09ld phase=%s\n",
        (int)getpid(),(long long)now.tv_sec,now.tv_nsec,phase);
    if (length<=0 || (size_t)length>=sizeof(line)) return;
    NSPTraceAppend(NSPTraceDocuments,line,(size_t)length);
    NSPTraceAppend(NSPTraceTemporary,line,(size_t)length);
}

__attribute__((constructor)) static void NSPTraceEntry(void) {
    const char *home=getenv("HOME"),*temporary=getenv("TMPDIR");
    if (home && home[0]) {
        char folder[1024];
        int length=snprintf(folder,sizeof(folder),"%s/Documents",home);
        if (length>0 && (size_t)length<sizeof(folder)) (void)mkdir(folder,0700);
        length=snprintf(folder,sizeof(folder),"%s/Documents/NewStatePort",home);
        if (length>0 && (size_t)length<sizeof(folder)) (void)mkdir(folder,0700);
        length=snprintf(NSPTraceDocuments,sizeof(NSPTraceDocuments),
            "%s/Documents/NewStatePort/startup-trace.log",home);
        if (length<0 || (size_t)length>=sizeof(NSPTraceDocuments)) NSPTraceDocuments[0]=0;
    }
    if (temporary && temporary[0]) {
        int length=snprintf(NSPTraceTemporary,sizeof(NSPTraceTemporary),
            "%s/NewStatePort-startup-trace.log",temporary);
        if (length<0 || (size_t)length>=sizeof(NSPTraceTemporary)) NSPTraceTemporary[0]=0;
    }
    NSPTraceRecord("constructor");
    dispatch_async(dispatch_get_main_queue(),^{
        NSPTraceRecord("main_queue");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            NSPTraceRecord("alive_3s");
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            NSPTraceRecord("alive_10s");
        });
    });
}
