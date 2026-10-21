// HUD hosting/bootstrap adapted from Lessica/TrollSpeed (MIT).
// See ThirdPartyNotices.txt. Game data access is independently implemented.
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include "RemoteReader.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cmath>

extern char **environ;
static NSString *NSPFolder;
static NSString *const NSPStop=@"org.newstateport.external.stop";
static NSString *const NSPVersion=@"0.2.0";

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
static void NSPReport(NSString *status,NSDictionary *extra) {
    NSMutableDictionary *report=[@{@"version":NSPVersion,@"helper_pid":@(getpid()),
        @"time":@(NSDate.date.timeIntervalSince1970),@"status":status?:@"",
        @"read_only":@YES,@"game_files_modified":@NO,@"injection":@NO,
        @"on_device_verified":@NO} mutableCopy];
    if (extra) [report addEntriesFromDictionary:extra];
    NSPWriteJSON(@"status.json",report);
}
static NSDictionary *NSPDefaults(void) {
    return @{@"sampling":@NO,@"characters":@YES,@"vehicles":@YES,@"items":@NO,
        @"boxes":@YES,@"lines":@NO,@"health":@YES,@"distances":@YES,@"names":@YES,
        @"crosshair":@YES,@"skeletons":@NO,@"orientation":@0};
}
static CGRect NSPScreenBounds(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return UIScreen.mainScreen.bounds;
#pragma clang diagnostic pop
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
    CGFloat width=self.bounds.size.width,height=self.bounds.size.height;
    CGContextRef c=UIGraphicsGetCurrentContext();
    if (!c) return;
    NSString *status=[@"NSP · " stringByAppendingString:self.status?:@"外置显示"];
    [status drawInRect:CGRectMake(12,10,MAX(0,width-24),52) withAttributes:@{
        NSFontAttributeName:[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightSemibold],
        NSForegroundColorAttributeName:UIColor.whiteColor,
        NSBackgroundColorAttributeName:[UIColor.blackColor colorWithAlphaComponent:0.65]}];
    if ([o[@"crosshair"] boolValue]) {
        CGContextSetStrokeColorWithColor(c,UIColor.whiteColor.CGColor);CGContextSetLineWidth(c,1);
        CGContextMoveToPoint(c,width/2-7,height/2);CGContextAddLineToPoint(c,width/2+7,height/2);
        CGContextMoveToPoint(c,width/2,height/2-7);CGContextAddLineToPoint(c,width/2,height/2+7);CGContextStrokePath(c);
    }
    auto snapshot=self.snapshot;
    if (!snapshot || !snapshot->cameraReady || ![o[@"sampling"] boolValue]) return;
    for (const auto &marker:snapshot->markers) {
        if (marker.kind==nsport::Kind::character && ![o[@"characters"] boolValue]) continue;
        if (marker.kind==nsport::Kind::vehicle && ![o[@"vehicles"] boolValue]) continue;
        if (marker.kind==nsport::Kind::item && ![o[@"items"] boolValue]) continue;
        nsport::Point p;
        if (!nsport::project(marker.position,snapshot->camera,(float)width,(float)height,p) ||
            p.x < -100 || p.x > width+100 || p.y < -100 || p.y > height+100) continue;
        UIColor *color=marker.kind==nsport::Kind::character ? UIColor.systemYellowColor :
            marker.kind==nsport::Kind::vehicle ? UIColor.systemCyanColor : UIColor.systemGreenColor;
        CGContextSetStrokeColorWithColor(c,color.CGColor);CGContextSetLineWidth(c,1.2);
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
        if ([o[@"distances"] boolValue]) [label appendFormat:@" %.0fm",nsport::distance(marker.position,snapshot->camera.position)/100];
        if ([o[@"health"] boolValue] && marker.hasHealth) [label appendFormat:@" %.0f/%.0f",marker.health,marker.maximum];
        [label drawAtPoint:CGPointMake(p.x+5,p.y+3) withAttributes:@{
            NSFontAttributeName:[UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightSemibold],
            NSForegroundColorAttributeName:color,NSBackgroundColorAttributeName:[UIColor.blackColor colorWithAlphaComponent:0.55]}];
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
    self.timer=[NSTimer scheduledTimerWithTimeInterval:0.2 repeats:YES block:^(NSTimer *timer) {
        (void)timer;[weak tick];
    }];
    [self tick];return YES;
}
- (void)tick {
    NSDictionary *settings=NSPReadJSON(@"settings.json")?:NSPDefaults();
    self.canvas.options=settings;
    CGSize screen=self.window.bounds.size;NSInteger orientation=[settings[@"orientation"] integerValue];
    self.canvas.transform=CGAffineTransformIdentity;
    self.canvas.bounds=CGRectMake(0,0,orientation==2 ? screen.width : screen.height,orientation==2 ? screen.height : screen.width);
    self.canvas.center=CGPointMake(screen.width/2,screen.height/2);
    if (orientation!=2) self.canvas.transform=CGAffineTransformMakeRotation(orientation==1 ? -M_PI_2 : M_PI_2);
    if (![settings[@"sampling"] boolValue]) {
        self.canvas.snapshot=nullptr;self.canvas.status=@"悬浮层已启动 · 游戏读取关闭";[self.canvas setNeedsDisplay];
        [self report:self.canvas.status extra:@{@"sampling":@NO,@"hosting_call_returned":@YES}];return;
    }
    if (_busy) return;
    _busy=YES;BOOL skeleton=[settings[@"skeletons"] boolValue];
    dispatch_async(_queue,^{
        @autoreleasepool {
            nsport::RemoteResult result;
            try {result=self->_reader.sample(skeleton);} catch (...) {result.status="远程读取异常，本次采样已停止";}
            dispatch_async(dispatch_get_main_queue(),^{
                self->_busy=NO;
                if (![self.canvas.options[@"sampling"] boolValue]) return;
                self.canvas.snapshot=result.snapshot;
                self.canvas.status=[NSString stringWithUTF8String:result.status.c_str()];
                [self.canvas setNeedsDisplay];
                NSMutableDictionary *extra=[@{@"sampling":@YES,@"target_pid":@(result.pid),
                    @"read_port_acquired":@(result.readPort),@"read_error":@(result.error),
                    @"hosting_call_returned":@YES} mutableCopy];
                if (result.snapshot) {
                    extra[@"camera_ready"]=@(result.snapshot->cameraReady);extra[@"markers"]=@(result.snapshot->markers.size());
                    extra[@"scanned"]=@(result.snapshot->scanned);extra[@"objects"]=@(result.snapshot->total);
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

@interface NSPControl : UIViewController
@property(nonatomic,strong) NSMutableDictionary *settings;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) NSTimer *timer;
@end
@implementation NSPControl
- (void)viewDidLoad {
    [super viewDidLoad];self.title=@"NSP 外置显示";self.view.backgroundColor=UIColor.systemBackgroundColor;
    self.settings=[(NSPReadJSON(@"settings.json")?:NSPDefaults()) mutableCopy];
    UIScrollView *scroll=[UIScrollView new];scroll.translatesAutoresizingMaskIntoConstraints=NO;[self.view addSubview:scroll];
    UIStackView *stack=[UIStackView new];stack.axis=UILayoutConstraintAxisVertical;stack.spacing=12;
    stack.translatesAutoresizingMaskIntoConstraints=NO;[scroll addSubview:stack];
    UILayoutGuide *safe=self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:safe.topAnchor],[scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],[scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-24],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:16],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-16],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-32]]];
    UILabel *intro=[UILabel new];intro.numberOfLines=0;intro.font=[UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    intro.text=@"NEW STATE 0.9.98（804）\nTrollStore · iOS 17.0 · 保持开发者模式关闭\n先启动悬浮层检查显示，再开启游戏读取。外置读取与跨应用显示尚待实机验证。";
    [stack addArrangedSubview:intro];
    self.status=[UILabel new];self.status.numberOfLines=0;self.status.font=[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
    [stack addArrangedSubview:self.status];
    for (NSArray *item in @[@[@"启动悬浮层",@"startHUD"],@[@"停止悬浮层",@"stopHUD"],@[@"导出状态",@"shareStatus"]]) {
        UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];[button setTitle:item[0] forState:UIControlStateNormal];
        [button addTarget:self action:NSSelectorFromString(item[1]) forControlEvents:UIControlEventTouchUpInside];[stack addArrangedSubview:button];
    }
    UISegmentedControl *orientation=[[UISegmentedControl alloc] initWithItems:@[@"横屏左",@"横屏右",@"竖屏"]];
    orientation.selectedSegmentIndex=[self.settings[@"orientation"] integerValue];
    [orientation addTarget:self action:@selector(orientationChanged:) forControlEvents:UIControlEventValueChanged];[stack addArrangedSubview:orientation];
    for (NSArray *item in @[@[@"sampling",@"开启游戏读取"],@[@"characters",@"角色（包含队友）"],@[@"vehicles",@"载具"],
        @[@"items",@"物资位置"],@[@"boxes",@"胶囊轮廓方框"],@[@"lines",@"连线"],@[@"health",@"血量"],
        @[@"distances",@"距离"],@[@"names",@"姓名"],@[@"crosshair",@"中心准星"],@[@"skeletons",@"骨架（实验性）"]]) {
        UILabel *label=[UILabel new];label.text=item[1];label.numberOfLines=0;
        UISwitch *toggle=[UISwitch new];toggle.accessibilityIdentifier=item[0];toggle.on=[self.settings[item[0]] boolValue];
        [toggle addTarget:self action:@selector(toggle:) forControlEvents:UIControlEventValueChanged];
        UIStackView *row=[[UIStackView alloc] initWithArrangedSubviews:@[label,toggle]];row.axis=UILayoutConstraintAxisHorizontal;
        row.alignment=UIStackViewAlignmentCenter;row.spacing=12;[stack addArrangedSubview:row];
    }
    UILabel *scope=[UILabel new];scope.numberOfLines=0;scope.font=[UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    scope.text=@"尚未实现：队伍区分、自瞄、后坐力修改、去雾、检测屏蔽。\n原游戏文件保持不变。本工具不保证游戏接受外部读取。\n窗口方向不合适时切换横屏方向。停止按钮只结束本工具的悬浮进程。\nHUD 启动方式参考 Lessica/TrollSpeed（MIT），许可随包提供。";[stack addArrangedSubview:scope];
    __weak NSPControl *weak=self;
    self.timer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {(void)timer;[weak refresh];}];
    [self save];[self refresh];
}
- (void)save {NSPWriteJSON(@"settings.json",self.settings);}
- (void)toggle:(UISwitch *)sender {self.settings[sender.accessibilityIdentifier]=@(sender.on);[self save];}
- (void)orientationChanged:(UISegmentedControl *)sender {self.settings[@"orientation"]=@(sender.selectedSegmentIndex);[self save];}
- (void)refresh {
    NSDictionary *report=NSPReadJSON(@"status.json");
    NSTimeInterval stamp=[report[@"time"] doubleValue];
    NSString *age=stamp ? [NSString stringWithFormat:@"%.0f 秒前",MAX(0,NSDate.date.timeIntervalSince1970-stamp)] : @"无记录";
    self.status.text=report ? [NSString stringWithFormat:@"%@\n更新：%@ · 读取错误：%@\n相机：%@ · 标记：%@",
        report[@"status"]?:@"",age,report[@"read_error"]?:@0,report[@"camera_ready"]?:@NO,report[@"markers"]?:@0] : @"悬浮层尚未启动";
}
- (void)stopHUD {CFNotificationCenterPostNotification(CFNotificationCenterGetDarwinNotifyCenter(),(__bridge CFStringRef)NSPStop,NULL,NULL,true);}
- (void)startHUD {
    [self save];[self stopHUD];
    self.status.text=@"正在启动外置进程…";
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{[self spawnHUD];});
}
- (void)spawnHUD {
    using Persona=int (*)(const posix_spawnattr_t *,uid_t,uint32_t);
    using Identity=int (*)(const posix_spawnattr_t *,uid_t);
    auto persona=reinterpret_cast<Persona>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_np"));
    auto uid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_uid_np"));
    auto gid=reinterpret_cast<Identity>(dlsym(RTLD_DEFAULT,"posix_spawnattr_set_persona_gid_np"));
    if (!persona || !uid || !gid) {self.status.text=@"系统不提供所需的辅助进程启动接口";return;}
    posix_spawnattr_t attributes;int rc=posix_spawnattr_init(&attributes);
    if (rc) {self.status.text=@"无法初始化辅助进程";return;}
    rc=persona(&attributes,99,1);if (!rc) rc=uid(&attributes,0);if (!rc) rc=gid(&attributes,0);
    NSString *executable=NSBundle.mainBundle.executablePath;
    const char *arguments[]={executable.fileSystemRepresentation,"--hud",NSPFolder.fileSystemRepresentation,NULL};
    pid_t child=0;
    if (!rc) rc=posix_spawn(&child,arguments[0],NULL,&attributes,const_cast<char **>(arguments),environ);
    posix_spawnattr_destroy(&attributes);
    if (rc) {self.status.text=[NSString stringWithFormat:@"外置进程启动失败：%d（需 TrollStore 安装权限）",rc];return;}
    self.status.text=[NSString stringWithFormat:@"外置进程已创建：%d，等待悬浮层状态",child];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        int status=0;pid_t reaped;
        do {reaped=waitpid(child,&status,0);} while (reaped<0 && errno==EINTR);
        if (reaped>0) {
            dispatch_async(dispatch_get_main_queue(),^{
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
    for (NSString *name in @[@"status.json",@"settings.json"]) {
        if ([NSFileManager.defaultManager fileExistsAtPath:NSPPath(name)]) [files addObject:[NSURL fileURLWithPath:NSPPath(name)]];
    }
    if (!files.count) {self.status.text=@"暂无状态文件可导出";return;}
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
    self.window.rootViewController=[[UINavigationController alloc] initWithRootViewController:[NSPControl new]];
    [self.window makeKeyAndVisible];return YES;
}
@end

static int NSPHUDMain(void) {
    NSPReport(@"外置进程已进入，正在初始化窗口服务",nil);
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
        if (argc==3 && !strcmp(argv[1],"--hud")) {
            NSPFolder=[NSString stringWithUTF8String:argv[2]];
            if (!NSPFolder.length || (![NSPFolder hasPrefix:@"/var/mobile/Containers/Data/Application/"] &&
                ![NSPFolder hasPrefix:@"/private/var/mobile/Containers/Data/Application/"])) return 5;
            return NSPHUDMain();
        }
        NSPFolder=[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/NSPExternal"];
        [NSFileManager.defaultManager createDirectoryAtPath:NSPFolder withIntermediateDirectories:YES attributes:nil error:nil];
        return UIApplicationMain(argc,argv,nil,NSStringFromClass(NSPAppDelegate.class));
    }
}
