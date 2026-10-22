// HUD hosting/bootstrap adapted from Lessica/TrollSpeed (MIT).
// See ThirdPartyNotices.txt. Game data access is independently implemented.
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include "RemoteReader.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cmath>

extern char **environ;
static NSString *NSPFolder;
static NSString *const NSPStop=@"org.newstateport.external.stop";
static NSString *const NSPVersion=@"0.3.2";
#ifdef NSP_UI_PREVIEW
static NSString *const NSPStorage=@"";
#else
static NSString *const NSPStorage=@"/var/mobile/Library/Application Support/org.newstateport.External";
#endif

@interface UIApplication (NSPPrivate)
- (void)_accessibilityInit;
- (void)__completeAndRunAsPlugin;
@end
@interface UIWindow (NSPPrivate)
- (unsigned int)_contextId;
@end

static NSString *NSPPath(NSString *name) {return [NSPFolder stringByAppendingPathComponent:name];}
static NSDictionary *NSPReadJSON(NSString *name) {
    NSData *data=[NSData dataWithContentsOfFile:NSPPath(name)];
    if (!data) return nil;
    id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    return [object isKindOfClass:NSDictionary.class] ? object : nil;
}
static void NSPWriteJSON(NSString *name,NSDictionary *object) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:object options:NSJSONWritingPrettyPrinted error:nil];
    if (data && [data writeToFile:NSPPath(name) atomically:YES])
        [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0644} ofItemAtPath:NSPPath(name) error:nil];
}
static NSDictionary *NSPRuntimePermissions(void) {
    using Create=CFTypeRef (*)(CFAllocatorRef);
    using Copy=CFTypeRef (*)(CFTypeRef,CFStringRef,CFErrorRef *);
    auto create=reinterpret_cast<Create>(dlsym(RTLD_DEFAULT,"SecTaskCreateFromSelf"));
    auto copy=reinterpret_cast<Copy>(dlsym(RTLD_DEFAULT,"SecTaskCopyValueForEntitlement"));
    NSMutableDictionary *values=[NSMutableDictionary dictionary];
    CFTypeRef task=create ? create(kCFAllocatorDefault) : nullptr;
    values[@"query_available"]=@(task && copy);
    if (task && copy) {
        for (NSString *key in @[@"platform-application",@"com.apple.private.persona-mgmt",
            @"com.apple.private.security.no-sandbox",@"com.apple.private.security.no-container",
            @"com.apple.private.security.container-required",@"com.apple.system-task-ports.read"]) {
            CFErrorRef error=nullptr;
            CFTypeRef value=copy(task,(__bridge CFStringRef)key,&error);
            if (value) {
                id object=CFBridgingRelease(value);
                if ([object isKindOfClass:NSNumber.class] || [object isKindOfClass:NSString.class]) values[key]=object;
            }
            if (error) CFRelease(error);
        }
    }
    if (task) CFRelease(task);
    return values;
}
static void NSPReport(NSString *status,NSDictionary *extra) {
    NSMutableDictionary *report=[@{@"version":NSPVersion,@"helper_pid":@(getpid()),
        @"time":@(NSDate.date.timeIntervalSince1970),@"status":status?:@"",
        @"read_only":@YES,@"game_files_modified":@NO,@"injection":@NO,
        @"on_device_verified":@NO,@"helper_uid":@(getuid()),@"helper_euid":@(geteuid())} mutableCopy];
    if (extra) [report addEntriesFromDictionary:extra];
    NSPWriteJSON(@"status.json",report);
}
static void NSPLaunchReport(NSString *phase,NSString *message,int error,pid_t child,NSDictionary *extra) {
    NSMutableDictionary *report=[@{@"version":NSPVersion,@"parent_pid":@(getpid()),
        @"parent_uid":@(getuid()),@"parent_euid":@(geteuid()),@"helper_pid":@(child),
        @"time":@(NSDate.date.timeIntervalSince1970),@"phase":phase,@"status":message,
        @"runtime_entitlements":NSPRuntimePermissions(),
        @"os":NSProcessInfo.processInfo.operatingSystemVersionString,
        @"error_code":@(error),@"error_text":error ? [NSString stringWithUTF8String:strerror(error)] : @""} mutableCopy];
    if (extra) [report addEntriesFromDictionary:extra];
    // The controller owns this file, so a child denied launch cannot suppress diagnostics.
    NSPWriteJSON(@"launch.json",report);
    if (!NSPReadJSON(@"status.json")) NSPWriteJSON(@"status.json",report);
}
static NSDictionary *NSPDefaults(void) {
    return @{@"sampling":@NO,@"characters":@YES,@"vehicles":@YES,@"items":@NO,
        @"boxes":@YES,@"lines":@NO,@"health":@YES,@"distances":@YES,@"names":@YES,
        @"crosshair":@YES,@"skeletons":@NO,@"orientation":@0,@"edge_indicators":@NO,@"health_bars":@YES,@"hud_status":@NO,
        @"label_scale":@1.0,@"overlay_opacity":@1.0,@"line_width":@1.2,
        @"hide_teammates":@YES,@"player_types":@YES};
}
static NSDictionary *NSPSettings(void) {
    NSMutableDictionary *settings=[NSPDefaults() mutableCopy];
    NSDictionary *stored=NSPReadJSON(@"settings.json");
    for (NSString *key in settings.allKeys) {
        id value=stored[key];if (![value isKindOfClass:NSNumber.class]) continue;
        double number=[value doubleValue];if (!std::isfinite(number)) continue;
        if ([key isEqualToString:@"label_scale"]) settings[key]=@(MAX(0.8,MIN(1.3,number)));
        else if ([key isEqualToString:@"overlay_opacity"]) settings[key]=@(MAX(0.35,MIN(1.0,number)));
        else if ([key isEqualToString:@"line_width"]) settings[key]=@(MAX(0.6,MIN(2.0,number)));
        else if ([key isEqualToString:@"orientation"]) settings[key]=@(MAX(0,MIN(2,[value integerValue])));
        else settings[key]=@([value boolValue]);
    }
    return settings;
}
static NSDictionary *NSPCheckSpawn(BOOL root) {
    NSString *mode=root ? @"root" : @"mobile";
    NSString *path=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"NSPSpawnProbe"];
    const char *args[]={path.fileSystemRepresentation,"--check",mode.UTF8String,NULL};
    NSMutableDictionary *result=[NSMutableDictionary dictionary];
    posix_spawnattr_t attributes;
    int error=posix_spawnattr_init(&attributes);
    if (error) return @{@"spawn_error":@(error),@"stage":@"probe_attributes"};
    if (root) {
        using Persona=int (*)(const posix_spawnattr_t *,uid_t,uint32_t);
        using Identity=int (*)(const posix_spawnattr_t *,uid_t);
        auto persona=reinterpret_cast<Persona>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_np"));
        auto uid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_uid_np"));
        auto gid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_gid_np"));
        error=(!persona || !uid || !gid) ? ENOSYS : persona(&attributes,99,1);
        if (!error) error=uid(&attributes,0);
        if (!error) error=gid(&attributes,0);
    }
    pid_t child=0;
    if (!error) error=posix_spawn(&child,args[0],NULL,&attributes,const_cast<char **>(args),environ);
    posix_spawnattr_destroy(&attributes);
    result[@"spawn_error"]=@(error);result[@"pid"]=@(child);
    if (!error) {
        int status=0;pid_t reaped;
        do {reaped=waitpid(child,&status,0);} while (reaped<0 && errno==EINTR);
        if (reaped==child) {
            result[@"wait_status"]=@(status);
            if (WIFEXITED(status)) result[@"exit_code"]=@(WEXITSTATUS(status));
            if (WIFSIGNALED(status)) result[@"exit_signal"]=@(WTERMSIG(status));
            NSDictionary *entry=NSPReadJSON([NSString stringWithFormat:@"spawn-check-%@.json",mode]);
            if ([entry[@"pid"] intValue]==child) result[@"entry"]=entry;
        } else result[@"wait_error"]=@(errno);
    }
    return result;
}
static CGRect NSPScreenBounds(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return UIScreen.mainScreen.bounds;
#pragma clang diagnostic pop
}
static UIColor *NSPCharacterColor(const nsport::Marker &marker) {
    if (marker.relation==nsport::Relation::teammate) return UIColor.systemGreenColor;
    if (marker.relation==nsport::Relation::unknown) return UIColor.systemOrangeColor;
    if (marker.playerKind==nsport::PlayerKind::human) return UIColor.systemRedColor;
    if (marker.playerKind==nsport::PlayerKind::bot) return UIColor.systemYellowColor;
    return UIColor.systemOrangeColor;
}
static NSString *NSPPlayerType(const nsport::Marker &marker) {
    if (marker.relation==nsport::Relation::teammate) return @"队友";
    NSString *type=marker.playerKind==nsport::PlayerKind::bot ? @"人机" : marker.playerKind==nsport::PlayerKind::human ? @"真人" : @"未知";
    return marker.relation==nsport::Relation::unknown ? [type stringByAppendingString:@"·队伍未知"] : type;
}

@interface NSPCanvas : UIView
@property(nonatomic) std::shared_ptr<nsport::Snapshot> snapshot;
@property(nonatomic,copy) NSDictionary *options;
@property(nonatomic,copy) NSString *status;
@end
@implementation NSPCanvas
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {(void)point;(void)event;return nil;}
- (void)drawRect:(CGRect)rect {
    (void)rect;
    NSDictionary *o=self.options?:NSPDefaults();
    CGFloat labelScale=MAX(0.8,MIN(1.3,o[@"label_scale"] ? [o[@"label_scale"] doubleValue] : 1.0));
    CGFloat lineWidth=MAX(0.6,MIN(2.0,o[@"line_width"] ? [o[@"line_width"] doubleValue] : 1.2));
    CGFloat width=self.bounds.size.width,height=self.bounds.size.height;
    CGContextRef c=UIGraphicsGetCurrentContext();
    if (!c) return;
    if ([o[@"hud_status"] boolValue]) {
        NSString *status=[@"NSP · " stringByAppendingString:self.status?:@"外置显示"];
        [status drawInRect:CGRectMake(12,10,MAX(0,width-24),52) withAttributes:@{
            NSFontAttributeName:[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightSemibold],
            NSForegroundColorAttributeName:UIColor.whiteColor,
            NSBackgroundColorAttributeName:[UIColor.blackColor colorWithAlphaComponent:0.65]}];
    }
    if ([o[@"crosshair"] boolValue]) {
        CGContextSetStrokeColorWithColor(c,UIColor.whiteColor.CGColor);CGContextSetLineWidth(c,1);
        CGContextMoveToPoint(c,width/2-7,height/2);CGContextAddLineToPoint(c,width/2+7,height/2);
        CGContextMoveToPoint(c,width/2,height/2-7);CGContextAddLineToPoint(c,width/2,height/2+7);CGContextStrokePath(c);
    }
    auto snapshot=self.snapshot;
    if (!snapshot || !snapshot->cameraReady || ![o[@"sampling"] boolValue]) return;
    for (const auto &marker:snapshot->markers) {
        if (marker.kind==nsport::Kind::character && ![o[@"characters"] boolValue]) continue;
        if (marker.kind==nsport::Kind::character && marker.relation==nsport::Relation::teammate && [o[@"hide_teammates"] boolValue]) continue;
        if (marker.kind==nsport::Kind::vehicle && ![o[@"vehicles"] boolValue]) continue;
        if (marker.kind==nsport::Kind::item && ![o[@"items"] boolValue]) continue;
        UIColor *color=marker.kind==nsport::Kind::character ? NSPCharacterColor(marker) :
            marker.kind==nsport::Kind::vehicle ? UIColor.systemCyanColor : UIColor.systemGreenColor;
        nsport::Point p;
        if (!nsport::project(marker.position,snapshot->camera,(float)width,(float)height,p) ||
            p.x<0 || p.x>width || p.y<0 || p.y>height) {
            nsport::EdgeIndicator edge;
            if (marker.kind==nsport::Kind::character && [o[@"edge_indicators"] boolValue] &&
                nsport::offscreenIndicator(marker.position,snapshot->camera,(float)width,(float)height,34,edge)) {
                CGFloat x=edge.position.x,y=edge.position.y,dx=edge.direction.x,dy=edge.direction.y;
                CGContextSetFillColorWithColor(c,color.CGColor);
                CGContextMoveToPoint(c,x+dx*8,y+dy*8);
                CGContextAddLineToPoint(c,x-dx*6-dy*5,y-dy*6+dx*5);
                CGContextAddLineToPoint(c,x-dx*6+dy*5,y-dy*6-dx*5);CGContextClosePath(c);CGContextFillPath(c);
                if ([o[@"distances"] boolValue]) {
                    NSString *distance=[NSString stringWithFormat:@"%.0fm",nsport::distance(marker.position,snapshot->camera.position)/100];
                    [distance drawAtPoint:CGPointMake(x-dx*20-18,y-dy*20-6) withAttributes:@{
                        NSFontAttributeName:[UIFont monospacedSystemFontOfSize:10*labelScale weight:UIFontWeightSemibold],
                        NSForegroundColorAttributeName:color,NSBackgroundColorAttributeName:[UIColor.blackColor colorWithAlphaComponent:0.55]}];
                }
            }
            continue;
        }
        CGContextSetStrokeColorWithColor(c,color.CGColor);CGContextSetLineWidth(c,lineWidth);
        if ([o[@"skeletons"] boolValue]) {
            for (const auto &bone:marker.bones) {
                nsport::Point a,b;
                if (nsport::project(bone.from,snapshot->camera,(float)width,(float)height,a) &&
                    nsport::project(bone.to,snapshot->camera,(float)width,(float)height,b)) {
                    CGContextMoveToPoint(c,a.x,a.y);CGContextAddLineToPoint(c,b.x,b.y);
                }
            }
            CGContextStrokePath(c);
        }
        if ([o[@"lines"] boolValue]) {
            CGContextMoveToPoint(c,width/2,height);CGContextAddLineToPoint(c,p.x,p.y);CGContextStrokePath(c);
        }
        if (marker.kind!=nsport::Kind::character)
            CGContextStrokeEllipseInRect(c,CGRectMake(p.x-3,p.y-3,6,6));
        if ([o[@"boxes"] boolValue] && marker.kind==nsport::Kind::character && marker.halfHeight>0) {
            nsport::Vec3 head=marker.position,feet=marker.position;head.z+=marker.halfHeight;feet.z-=marker.halfHeight;
            nsport::Point top,bottom;
            if (nsport::project(head,snapshot->camera,(float)width,(float)height,top) &&
                nsport::project(feet,snapshot->camera,(float)width,(float)height,bottom)) {
                CGFloat h=fabs(bottom.y-top.y),w=h*0.45;
                if (h>2 && h<height*2) CGContextStrokeRect(c,CGRectMake((top.x+bottom.x)/2-w/2,MIN(top.y,bottom.y),w,h));
            }
        }
        NSString *name=marker.kind==nsport::Kind::character ? @"角色" : marker.kind==nsport::Kind::vehicle ? @"载具" : @"物资";
        if ([o[@"names"] boolValue] && !marker.playerName.empty()) {
            NSString *value=[[NSString alloc] initWithCharacters:(const unichar *)marker.playerName.data() length:marker.playerName.size()];
            if (value.length) name=value;
        }
        NSMutableString *label=[name mutableCopy];
        if (marker.kind==nsport::Kind::character && [o[@"player_types"] boolValue]) {
            NSString *type=NSPPlayerType(marker);
            label=[name isEqualToString:@"角色"] ? [type mutableCopy] : [NSMutableString stringWithFormat:@"%@ · %@",type,name];
        }
        if ([o[@"distances"] boolValue]) [label appendFormat:@" %.0fm",nsport::distance(marker.position,snapshot->camera.position)/100];
        if ([o[@"health"] boolValue] && marker.hasHealth) [label appendFormat:@" %.0f/%.0f",marker.health,marker.maximum];
        [label drawAtPoint:CGPointMake(p.x+5,p.y+3) withAttributes:@{
            NSFontAttributeName:[UIFont monospacedSystemFontOfSize:11*labelScale weight:UIFontWeightSemibold],
            NSForegroundColorAttributeName:color,NSBackgroundColorAttributeName:[UIColor.blackColor colorWithAlphaComponent:0.55]}];
        float fraction=0;
        if (marker.kind==nsport::Kind::character && [o[@"health_bars"] boolValue] && marker.hasHealth &&
            nsport::healthFraction(marker.health,marker.maximum,fraction)) {
            CGRect bar=CGRectMake(p.x+5,p.y+3+16*labelScale,48*labelScale,4);
            CGContextSetFillColorWithColor(c,[UIColor.blackColor colorWithAlphaComponent:0.75].CGColor);CGContextFillRect(c,bar);
            UIColor *fill=fraction<=0.25f ? UIColor.systemRedColor : fraction<=0.5f ? UIColor.systemOrangeColor : UIColor.systemGreenColor;
            CGContextSetFillColorWithColor(c,fill.CGColor);CGContextFillRect(c,CGRectMake(bar.origin.x,bar.origin.y,bar.size.width*fraction,bar.size.height));
        }
    }
}
@end

@interface NSPHUDWindow : UIWindow
@end
@implementation NSPHUDWindow
+ (BOOL)_isSystemWindow {return YES;}
- (BOOL)_isWindowServerHostingManaged {return NO;}
- (BOOL)_ignoresHitTest {return YES;}
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {(void)point;(void)event;return nil;}
@end

@interface NSPHUDDelegate : UIResponder <UIApplicationDelegate> {
    nsport::RemoteReader _reader;
    dispatch_queue_t _queue;
    BOOL _busy;
    NSTimeInterval _lastReport;
    NSString *_lastStatus;
    NSTimeInterval _lastSettingsLoad,_lastReadStart,_lastFrameReceived;
}
@property(nonatomic,strong) UIWindow *window;
@property(nonatomic,strong) NSPCanvas *canvas;
@property(nonatomic,strong) id host;
@property(nonatomic,strong) NSTimer *timer;
- (void)stop;
@end
static void NSPStopCallback(CFNotificationCenterRef center,void *observer,CFStringRef name,const void *object,CFDictionaryRef info) {
    (void)center;(void)name;(void)object;(void)info;
    NSPHUDDelegate *delegate=(__bridge NSPHUDDelegate *)observer;
    dispatch_async(dispatch_get_main_queue(),^{[delegate stop];});
}
@implementation NSPHUDDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;(void)options;
    NSPReport(@"正在注册跨应用悬浮层",nil);
    CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),(__bridge const void *)self,
        NSPStopCallback,(__bridge CFStringRef)NSPStop,NULL,CFNotificationSuspensionBehaviorDeliverImmediately);
    self.window=[[NSPHUDWindow alloc] initWithFrame:NSPScreenBounds()];
    self.window.backgroundColor=UIColor.clearColor;self.window.windowLevel=10000010.0;
    UIViewController *controller=[UIViewController new];controller.view.backgroundColor=UIColor.clearColor;
    self.canvas=[[NSPCanvas alloc] initWithFrame:self.window.bounds];
    self.canvas.backgroundColor=UIColor.clearColor;self.canvas.opaque=NO;self.canvas.userInteractionEnabled=NO;
    [controller.view addSubview:self.canvas];self.window.rootViewController=controller;
    [self.window makeKeyAndVisible];
    Class cls=NSClassFromString(@"SBSAccessibilityWindowHostingController");
    SEL selector=NSSelectorFromString(@"registerWindowWithContextID:atLevel:");
    self.host=cls ? [[cls alloc] init] : nil;
    if (!self.host || ![self.host respondsToSelector:selector] || ![self.window respondsToSelector:@selector(_contextId)]) {
        NSPReport(@"跨应用窗口接口不可用，悬浮层未注册",@{@"overlay_registered":@NO});self.window.hidden=YES;return YES;
    }
    unsigned context=[self.window _contextId];double level=self.window.windowLevel;
    if (!context) {NSPReport(@"窗口上下文尚未建立，悬浮层未注册",@{@"overlay_registered":@NO});self.window.hidden=YES;return YES;}
    NSInvocation *call=[NSInvocation invocationWithMethodSignature:[NSMethodSignature signatureWithObjCTypes:"v@:Id"]];
    call.target=self.host;call.selector=selector;[call setArgument:&context atIndex:2];[call setArgument:&level atIndex:3];[call invoke];
    _queue=dispatch_queue_create("org.newstateport.external.read",DISPATCH_QUEUE_SERIAL);
    __weak NSPHUDDelegate *weak=self;
    self.timer=[NSTimer scheduledTimerWithTimeInterval:1.0/60.0 repeats:YES block:^(NSTimer *timer) {
        (void)timer;[weak tick];
    }];
    [self tick];return YES;
}
- (void)tick {
    NSTimeInterval uptime=NSProcessInfo.processInfo.systemUptime;
    NSDictionary *settings=self.canvas.options;
    if (!settings || uptime-_lastSettingsLoad>=0.25) {
        settings=NSPSettings();_lastSettingsLoad=uptime;
    }
    self.canvas.options=settings;
    self.canvas.alpha=MAX(0.35,MIN(1.0,settings[@"overlay_opacity"] ? [settings[@"overlay_opacity"] doubleValue] : 1.0));
    CGSize screen=self.window.bounds.size;NSInteger orientation=[settings[@"orientation"] integerValue];
    self.canvas.transform=CGAffineTransformIdentity;
    self.canvas.bounds=CGRectMake(0,0,orientation==2 ? screen.width : screen.height,orientation==2 ? screen.height : screen.width);
    self.canvas.center=CGPointMake(screen.width/2,screen.height/2);
    if (orientation!=2) self.canvas.transform=CGAffineTransformMakeRotation(orientation==1 ? -M_PI_2 : M_PI_2);
    if (![settings[@"sampling"] boolValue]) {
        self.canvas.snapshot=nullptr;self.canvas.status=@"悬浮层已启动 · 游戏读取关闭";[self.canvas setNeedsDisplay];
        [self report:self.canvas.status extra:@{@"sampling":@NO,@"hosting_call_returned":@YES}];return;
    }
    if (_busy) {
        if (uptime-_lastFrameReceived>0.25) {self.canvas.snapshot=nullptr;[self.canvas setNeedsDisplay];}
        return;
    }
    _busy=YES;BOOL skeleton=[settings[@"skeletons"] boolValue];
    nsport::SampleOptions requested;requested.skeletons=skeleton;requested.kinds=0;
    requested.hideTeammates=[settings[@"hide_teammates"] boolValue];
    if ([settings[@"characters"] boolValue]) requested.kinds|=1;
    if ([settings[@"vehicles"] boolValue]) requested.kinds|=2;
    if ([settings[@"items"] boolValue]) requested.kinds|=4;
    dispatch_async(_queue,^{
        @autoreleasepool {
            nsport::RemoteResult result;
            NSTimeInterval start=NSProcessInfo.processInfo.systemUptime;
            double interval=self->_lastReadStart ? (start-self->_lastReadStart)*1000 : 0;
            self->_lastReadStart=start;
            try {result=self->_reader.sample(requested);} catch (...) {result.status="远程读取异常，本次采样已停止";}
            double elapsed=(NSProcessInfo.processInfo.systemUptime-start)*1000;
            dispatch_async(dispatch_get_main_queue(),^{
                self->_busy=NO;
                if (![self.canvas.options[@"sampling"] boolValue]) return;
                self->_lastFrameReceived=NSProcessInfo.processInfo.systemUptime;
                BOOL expired=self->_lastFrameReceived-start>0.25;
                self.canvas.snapshot=expired ? nullptr : result.snapshot;
                self.canvas.status=expired ? @"采样结果已过期，等待更新" : [NSString stringWithUTF8String:result.status.c_str()];
                [self.canvas setNeedsDisplay];
                NSMutableDictionary *extra=[@{@"sampling":@YES,@"target_pid":@(result.pid),
                    @"read_port_acquired":@(result.readPort),@"read_error":@(result.error),
                    @"hosting_call_returned":@YES} mutableCopy];
                extra[@"performance"]=@{@"read_ms":@(elapsed),@"sample_interval_ms":@(interval),
                    @"target_hz":@60,@"discovery_budget":@512,@"player_discovery_budget":@32,
                    @"details_refreshed":@(result.detailsRefreshed),@"snapshot_expired":@(expired)};
                if (!result.images.stage.empty()) {
                    NSMutableArray *uuids=[NSMutableArray array];
                    for (const auto &uuid:result.images.executableUUIDs) [uuids addObject:[NSString stringWithUTF8String:uuid.c_str()]];
                    const auto &d=result.images;
                    extra[@"image_lookup"]=@{
                        @"stage":[NSString stringWithUTF8String:d.stage.c_str()],
                        @"expected_uuid":@"AF49432E-9F7F-3D7C-9470-8CF90E3B3378",@"executable_uuids":uuids,
                        @"dyld_info_address":@(result.dyldInfoAddress),@"dyld_info_size":@(result.dyldInfoSize),
                        @"dyld_info_format":@(result.dyldInfoFormat),@"dyld_version":@(d.version),
                        @"image_count":@(d.count),@"image_array":@(d.array),@"images_scanned":@(d.scanned),
                        @"unreadable_headers":@(d.unreadableHeaders),@"executable_headers":@(d.executableHeaders),
                        @"command_read_failures":@(d.commandReadFailures),@"malformed_commands":@(d.malformedCommands),
                        @"failed_read_address":@(result.failedReadAddress),@"failed_read_size":@(result.failedReadSize),
                        @"copied_read_size":@(result.copiedReadSize),@"vm_read_error":@(result.error)};
                }
                if (result.snapshot) {
                    extra[@"affiliation"]=@{@"local_team_known":@(result.snapshot->localTeamKnown),
                        @"teammates_hidden":@(result.snapshot->teammatesHidden),@"dead_hidden":@(result.snapshot->deadHidden),
                        @"state_unreadable":@(result.snapshot->stateUnreadable)};
                    extra[@"discovery"]=@{@"player_array_available":@(result.snapshot->playerArrayAvailable),
                        @"players_checked":@(result.snapshot->playersChecked),@"players_discovered":@(result.snapshot->playersDiscovered)};
                    extra[@"camera_ready"]=@(result.snapshot->cameraReady && !expired);extra[@"markers"]=@(expired ? 0 : result.snapshot->markers.size());
                    extra[@"scanned"]=@(result.snapshot->scanned);extra[@"objects"]=@(result.snapshot->total);
                    size_t characters=0,vehicles=0,items=0,health=0,skeletons=0,bones=0,humans=0,bots=0,unknown=0;
                    for (const auto &marker:result.snapshot->markers) {
                        if (expired) break;
                        if (marker.kind==nsport::Kind::character) {
                            ++characters;if (marker.hasHealth) ++health;
                            if (marker.playerKind==nsport::PlayerKind::human) ++humans;
                            else if (marker.playerKind==nsport::PlayerKind::bot) ++bots;
                            else ++unknown;
                            if (!marker.bones.empty()) ++skeletons;
                            bones+=marker.bones.size();
                        } else if (marker.kind==nsport::Kind::vehicle) ++vehicles;
                        else if (marker.kind==nsport::Kind::item) ++items;
                    }
                    extra[@"marker_counts"]=@{@"characters":@(characters),@"vehicles":@(vehicles),@"items":@(items),
                        @"health_readable":@(health),@"skeletons_readable":@(skeletons),@"bone_segments":@(bones),
                        @"humans":@(humans),@"bots":@(bots),@"unknown":@(unknown)};
                    extra[@"skeleton_sampling_enabled"]=@(skeleton);
                }
                [self report:self.canvas.status extra:extra];
            });
        }
    });
}
- (void)report:(NSString *)status extra:(NSDictionary *)extra {
    NSTimeInterval now=NSDate.date.timeIntervalSince1970;
    if (now-_lastReport>=2 || ![_lastStatus isEqualToString:status]) {
        _lastReport=now;_lastStatus=[status copy];NSPReport(status,extra);
    }
}
- (void)stop {
    [self.timer invalidate];self.window.hidden=YES;
    NSPReport(@"外置悬浮层已停止",@{@"stopped":@YES});
    exit(0); // Terminate only our own HUD process, never the game.
}
@end

#include "ControlPanel.inc"

@implementation NSPControl (Runtime)
- (void)checkMemory {
    if (self.checkingMemory) return;
    self.checkingMemory=YES;
    // Keep the running HUD and its existing read-only permissions separate from the probe.
    using Persona=int (*)(const posix_spawnattr_t *,uid_t,uint32_t);
    using Identity=int (*)(const posix_spawnattr_t *,uid_t);
    auto persona=reinterpret_cast<Persona>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_np"));
    auto uid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_uid_np"));
    auto gid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_gid_np"));
    NSMutableDictionary *launch=[@{@"version":NSPVersion,@"time":@(NSDate.date.timeIntervalSince1970),
        @"parent_uid":@(getuid()),@"parent_euid":@(geteuid()),@"status":@"正在启动内存权限验证程序"} mutableCopy];
    NSPWriteJSON(@"memory-check-launch.json",launch);
    [NSFileManager.defaultManager removeItemAtPath:NSPPath(@"memory-check.json") error:nil];
    posix_spawnattr_t attributes;int initialized=posix_spawnattr_init(&attributes);int rc=initialized;
    if (!rc) rc=(!persona || !uid || !gid) ? ENOSYS : persona(&attributes,99,1);
    if (!rc) rc=uid(&attributes,0);
    if (!rc) rc=gid(&attributes,0);
    NSString *executable=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"NSPMemoryCheck"];
    const char *args[]={executable.fileSystemRepresentation,"--run",nullptr};pid_t child=0;
    if (!rc) rc=posix_spawn(&child,args[0],nullptr,&attributes,const_cast<char **>(args),environ);
    if (!initialized) posix_spawnattr_destroy(&attributes);
    launch[@"spawn_return"]=@(rc);launch[@"probe_pid"]=@(child);
    launch[@"status"]=rc ? [NSString stringWithFormat:@"验证程序启动失败：%d（%s）",rc,strerror(rc)] : @"验证程序运行中";
    NSPWriteJSON(@"memory-check-launch.json",launch);[self refresh];
    if (rc) {self.checkingMemory=NO;return;}
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        int status=0;pid_t waited;
        do {waited=waitpid(child,&status,0);} while (waited<0 && errno==EINTR);
        dispatch_async(dispatch_get_main_queue(),^{
            self.checkingMemory=NO;
            NSMutableDictionary *result=[NSPReadJSON(@"memory-check-launch.json") mutableCopy];
            if ([result[@"probe_pid"] intValue]==child) {
                result[@"wait_status"]=@(status);
                if (waited==child && WIFEXITED(status)) {
                    result[@"exit_code"]=@(WEXITSTATUS(status));
                    result[@"status"]=[NSString stringWithFormat:@"验证程序已退出，代码 %d；结果见导出文件",WEXITSTATUS(status)];
                } else if (waited==child && WIFSIGNALED(status)) {
                    result[@"exit_signal"]=@(WTERMSIG(status));
                    result[@"status"]=[NSString stringWithFormat:@"验证程序被信号 %d 终止",WTERMSIG(status)];
                } else result[@"status"]=@"无法取得验证程序退出状态";
                NSPWriteJSON(@"memory-check-launch.json",result);
            }
            [self refresh];
        });
    });
}
- (void)stopHUD {
    ++self.launchGeneration;self.startingHUD=NO;
    CFNotificationCenterPostNotification(CFNotificationCenterGetDarwinNotifyCenter(),(__bridge CFStringRef)NSPStop,NULL,NULL,true);
    NSPLaunchReport(@"stop_requested",@"已请求停止悬浮层",0,0,nil);[self refresh];
}
- (void)startHUD {
    if (self.startingHUD) return;
    [self save];[self stopHUD];self.startingHUD=YES;
    NSUInteger generation=self.launchGeneration;
    NSPLaunchReport(@"requested",@"正在启动外置进程…",0,0,nil);[self refresh];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{
        if (generation!=self.launchGeneration) return;
        self.startingHUD=NO;[self spawnHUD];
    });
}
- (void)spawnHUD {
    using Persona=int (*)(const posix_spawnattr_t *,uid_t,uint32_t);
    using Identity=int (*)(const posix_spawnattr_t *,uid_t);
    auto persona=reinterpret_cast<Persona>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_np"));
    auto uid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_uid_np"));
    auto gid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_gid_np"));
    if (!persona || !uid || !gid) {
        NSPLaunchReport(@"persona_api_missing",@"系统不提供所需的辅助进程启动接口",0,0,
            @{@"persona_api":@(persona!=nullptr),@"uid_api":@(uid!=nullptr),@"gid_api":@(gid!=nullptr)});
        [self refresh];return;
    }
    posix_spawnattr_t attributes;int rc=posix_spawnattr_init(&attributes);
    if (rc) {NSPLaunchReport(@"spawnattr_init",@"无法初始化辅助进程",rc,0,nil);[self refresh];return;}
    NSString *stage=@"persona";
    rc=persona(&attributes,99,1);
    if (!rc) {stage=@"persona_uid";rc=uid(&attributes,0);}
    if (!rc) {stage=@"persona_gid";rc=gid(&attributes,0);}
    NSString *executable=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"NSPExternalHUD"];
    struct stat executableInfo{};
    errno=0;int statResult=stat(executable.fileSystemRepresentation,&executableInfo);int statError=errno;
    NSDictionary *helperInfo=@{@"helper_exists":@(statResult==0),@"helper_stat_error":@(statResult ? statError : 0),
        @"helper_mode":@(statResult ? 0 : executableInfo.st_mode & 07777),
        @"helper_owner":@(statResult ? -1 : static_cast<int>(executableInfo.st_uid))};
    const char *arguments[]={executable.fileSystemRepresentation,"--hud",NSPFolder.fileSystemRepresentation,NULL};
    pid_t child=0;
    if (!rc) {stage=@"posix_spawn";rc=posix_spawn(&child,arguments[0],NULL,&attributes,const_cast<char **>(arguments),environ);}
    posix_spawnattr_destroy(&attributes);
    if (rc) {
        NSPLaunchReport(stage,[NSString stringWithFormat:@"外置进程启动失败：%d（%s）；尚不能确定是权限问题",rc,strerror(rc)],rc,0,helperInfo);
        [self refresh];
        if (rc==EPERM && [stage isEqualToString:@"posix_spawn"]) {
            NSNumber *attempt=NSPReadJSON(@"launch.json")[@"time"];
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
                @autoreleasepool {
                    NSDictionary *mobile=NSPCheckSpawn(NO);
                    NSDictionary *root=NSPCheckSpawn(YES);
                    dispatch_async(dispatch_get_main_queue(),^{
                        NSMutableDictionary *launch=[NSPReadJSON(@"launch.json") mutableCopy];
                        if ([launch[@"time"] isEqual:attempt]) {
                            launch[@"spawn_probes"]=@{@"mobile":mobile,@"root":root};
                            NSPWriteJSON(@"launch.json",launch);[self refresh];
                        }
                    });
                }
            });
        }
        return;
    }
    NSPLaunchReport(@"child_created",[NSString stringWithFormat:@"外置进程已创建：%d，等待悬浮层状态",child],0,child,helperInfo);
    [self refresh];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        int status=0;pid_t reaped;
        do {reaped=waitpid(child,&status,0);} while (reaped<0 && errno==EINTR);
        if (reaped>0) {
            dispatch_async(dispatch_get_main_queue(),^{
                NSDictionary *launch=NSPReadJSON(@"launch.json");
                if ([launch[@"helper_pid"] intValue]==child) {
                    NSMutableDictionary *extra=[@{@"helper_wait_status":@(status)} mutableCopy];
                    NSString *message;
                    if (WIFSIGNALED(status)) {extra[@"exit_signal"]=@(WTERMSIG(status));message=[NSString stringWithFormat:@"辅助进程被信号 %d 终止",WTERMSIG(status)];}
                    else if (WIFEXITED(status)) {extra[@"exit_code"]=@(WEXITSTATUS(status));message=[NSString stringWithFormat:@"辅助进程已退出，退出码 %d",WEXITSTATUS(status)];}
                    else message=[NSString stringWithFormat:@"辅助进程状态：%d",status];
                    NSPLaunchReport(@"child_exited",message,0,child,extra);
                }
                NSDictionary *old=NSPReadJSON(@"status.json");
                if ([old[@"helper_pid"] intValue]==child && ![old[@"stopped"] boolValue]) {
                    NSMutableDictionary *report=[old mutableCopy];report[@"helper_wait_status"]=@(status);
                    report[@"status"]=[NSString stringWithFormat:@"外置进程已退出（状态 %d）；%@",status,old[@"status"]?:@""];
                    NSPWriteJSON(@"status.json",report);
                }
                [self refresh];
            });
        }
    });
}
- (void)shareStatus {
    NSMutableArray *files=[NSMutableArray array];
    for (NSString *name in @[@"status.json",@"settings.json",@"launch.json",@"spawn-check-mobile.json",@"spawn-check-root.json",
        @"memory-check.json",@"memory-check-launch.json"]) {
        if ([NSFileManager.defaultManager fileExistsAtPath:NSPPath(name)]) [files addObject:[NSURL fileURLWithPath:NSPPath(name)]];
    }
    if (!files.count) {
        UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"暂无诊断记录" message:@"启动悬浮层后再导出。" preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"知道了" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];return;
    }
    UIActivityViewController *share=[[UIActivityViewController alloc] initWithActivityItems:files applicationActivities:nil];
    share.popoverPresentationController.sourceView=self.view;
    share.popoverPresentationController.sourceRect=CGRectMake(self.view.bounds.size.width/2,self.view.bounds.size.height/2,1,1);
    [self presentViewController:share animated:YES completion:nil];
}
@end

@interface NSPAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation NSPAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;(void)options;
    self.window=[[UIWindow alloc] initWithFrame:NSPScreenBounds()];
    self.window.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;
    self.window.rootViewController=[[UINavigationController alloc] initWithRootViewController:[NSPControl new]];
    [self.window makeKeyAndVisible];return YES;
}
@end

[[maybe_unused]] static int NSPHUDMain(void) {
    NSPReport(@"外置进程已进入，正在初始化窗口服务",@{@"runtime_entitlements":NSPRuntimePermissions()});
    if (geteuid()!=0) {NSPReport(@"辅助进程未以 root 身份启动，已停止初始化",nil);return 7;}
    // Same private HUD entry sequence used by TrollSpeed; no code injection.
    void *gs=dlopen("/System/Library/PrivateFrameworks/GraphicsServices.framework/GraphicsServices",RTLD_NOW);
    void *bks=dlopen("/System/Library/PrivateFrameworks/BackBoardServices.framework/BackBoardServices",RTLD_NOW);
    void *sbs=dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices",RTLD_NOW);
    using Init=void (*)(void);using Instantiate=void (*)(Class);using Events=void (*)(Boolean);using Mode=void (*)(CFStringRef);
    auto gsInit=reinterpret_cast<Init>(gs ? dlsym(gs,"GSInitialize") : nullptr);
    auto bksInit=reinterpret_cast<Init>(bks ? dlsym(bks,"BKSDisplayServicesStart") : nullptr);
    auto appInit=reinterpret_cast<Init>(dlsym(RTLD_DEFAULT,"UIApplicationInitialize"));
    auto instantiate=reinterpret_cast<Instantiate>(dlsym(RTLD_DEFAULT,"UIApplicationInstantiateSingleton"));
    if (!sbs || !gsInit || !bksInit || !appInit || !instantiate) {NSPReport(@"外置窗口所需的系统接口缺失",nil);return 2;}
    @try {
        (void)NSPScreenBounds();(void)CFRunLoopGetCurrent();gsInit();bksInit();appInit();instantiate(UIApplication.class);
        static NSPHUDDelegate *delegate;delegate=[NSPHUDDelegate new];
        UIApplication *application=UIApplication.sharedApplication;application.delegate=delegate;
        if ([application respondsToSelector:@selector(_accessibilityInit)]) [application _accessibilityInit];
        auto events=reinterpret_cast<Events>(dlsym(gs,"GSEventInitialize"));
        auto mode=reinterpret_cast<Mode>(dlsym(gs,"GSEventPushRunLoopMode"));
        if (events) events(false);if (mode) mode(kCFRunLoopDefaultMode);
        if (![application respondsToSelector:@selector(__completeAndRunAsPlugin)]) {NSPReport(@"系统不支持独立 HUD 运行入口",nil);return 3;}
        [application __completeAndRunAsPlugin];CFRunLoopRun();
    } @catch (NSException *exception) {
        NSPReport([NSString stringWithFormat:@"外置窗口异常：%@：%@",exception.name,exception.reason],nil);return 4;
    }
    return 0;
}
int main(int argc,char *argv[]) {
    @autoreleasepool {
#ifdef NSP_HUD_HELPER
        if (argc==3 && !strcmp(argv[1],"--hud")) {
            NSPFolder=[NSString stringWithUTF8String:argv[2]];
            if (![NSPFolder isEqualToString:NSPStorage]) return 5;
            return NSPHUDMain();
        }
        return 6; // This executable is a HUD helper, not a second launcher app.
#else
#ifdef NSP_UI_PREVIEW
        NSPFolder=[NSTemporaryDirectory() stringByAppendingPathComponent:@"NSPPreview"];
#else
        NSPFolder=NSPStorage;
#endif
        [NSFileManager.defaultManager createDirectoryAtPath:NSPFolder withIntermediateDirectories:YES attributes:nil error:nil];
        return UIApplicationMain(argc,argv,nil,NSStringFromClass(NSPAppDelegate.class));
#endif
    }
}
