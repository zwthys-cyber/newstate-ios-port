#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#include "PortCore.hpp"
#include <memory>
#include <cmath>

using namespace nsport;

static bool NSPRead(void *, uint64_t address, void *buffer, size_t size) {
    vm_size_t copied=0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)address, (vm_size_t)size,
        (vm_address_t)buffer, &copied)==KERN_SUCCESS && copied==size;
}
static NSString *const NSPUUID=@"AF49432E-9F7F-3D7C-9470-8CF90E3B3378";
static NSString *NSPValidate(void) {
    NSBundle *bundle=NSBundle.mainBundle;
    if (![bundle.bundleIdentifier isEqualToString:@"com.pubg.newstate"]) return @"不属于目标游戏";
    if (![bundle.infoDictionary[@"CFBundleShortVersionString"] isEqualToString:@"0.9.98"] ||
        ![bundle.infoDictionary[@"CFBundleVersion"] isEqualToString:@"804"]) return @"仅适配 0.9.98（804）";
    const struct mach_header_64 *header=(const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!header || header->magic!=MH_MAGIC_64 || header->cputype!=CPU_TYPE_ARM64 || header->sizeofcmds>65536)
        return @"主程序架构不匹配";
    const uint8_t *cursor=(const uint8_t *)(header+1),*end=cursor+header->sizeofcmds;
    BOOL matched=NO;
    for (uint32_t i=0;i<header->ncmds;++i) {
        if (cursor+sizeof(struct load_command)>end) return @"加载命令损坏";
        const struct load_command *command=(const struct load_command *)cursor;
        if (command->cmdsize<sizeof(*command) || command->cmdsize>(size_t)(end-cursor)) return @"加载命令损坏";
        if (command->cmd==LC_UUID && command->cmdsize>=sizeof(struct uuid_command)) {
            const struct uuid_command *uuid=(const struct uuid_command *)command;
            matched=[[[NSUUID alloc] initWithUUIDBytes:uuid->uuid].UUIDString isEqualToString:NSPUUID];
        }
        cursor+=command->cmdsize;
    }
    return matched ? nil : @"主程序 UUID 与分析样本不符";
}
static void NSPLog(NSString *text) {
    NSString *folder=[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/NewStatePort"];
    [NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *path=[folder stringByAppendingPathComponent:@"load.log"];
    NSString *line=[NSString stringWithFormat:@"%@ %@\n",NSDate.date,text];
    NSData *data=[line dataUsingEncoding:NSUTF8StringEncoding];
    if (![NSFileManager.defaultManager fileExistsAtPath:path]) [data writeToFile:path atomically:YES];
    else {
        NSFileHandle *handle=[NSFileHandle fileHandleForWritingAtPath:path];
        @try {[handle seekToEndOfFile];[handle writeData:data];[handle closeFile];} @catch (NSException *exception) {(void)exception;}
    }
}

@interface NSPOverlay : UIView
@property(nonatomic) std::shared_ptr<Snapshot> snapshot;
@property(nonatomic) BOOL characters,vehicles,items,boxes,lines,health,distances,names,crosshair;
@end
@implementation NSPOverlay
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *view=[super hitTest:point withEvent:event];
    return view==self ? nil : view;
}
- (void)drawRect:(CGRect)rect {
    (void)rect;
    auto snapshot=self.snapshot;
    if (!snapshot || !snapshot->cameraReady) return;
    CGContextRef context=UIGraphicsGetCurrentContext();
    CGFloat width=self.bounds.size.width,height=self.bounds.size.height;
    if (self.crosshair) {
        CGContextSetStrokeColorWithColor(context,UIColor.whiteColor.CGColor);
        CGContextSetLineWidth(context,1);
        CGContextMoveToPoint(context,width/2-7,height/2);CGContextAddLineToPoint(context,width/2+7,height/2);
        CGContextMoveToPoint(context,width/2,height/2-7);CGContextAddLineToPoint(context,width/2,height/2+7);
        CGContextStrokePath(context);
    }
    for (const Marker &marker:snapshot->markers) {
        if (marker.kind==Kind::character && !self.characters) continue;
        if (marker.kind==Kind::vehicle && !self.vehicles) continue;
        if (marker.kind==Kind::item && !self.items) continue;
        nsport::Point p;
        if (!project(marker.position,snapshot->camera,(float)width,(float)height,p)) continue;
        if (p.x < -100 || p.x > width+100 || p.y < -100 || p.y > height+100) continue;
        UIColor *color=marker.kind==Kind::character ? UIColor.systemYellowColor :
            marker.kind==Kind::vehicle ? UIColor.systemCyanColor : UIColor.systemGreenColor;
        CGContextSetStrokeColorWithColor(context,color.CGColor);
        CGContextSetLineWidth(context,1.2);
        if (self.lines) {
            CGContextMoveToPoint(context,width/2,height);
            CGContextAddLineToPoint(context,p.x,p.y);CGContextStrokePath(context);
        }
        CGContextStrokeEllipseInRect(context,CGRectMake(p.x-3,p.y-3,6,6));
        if (self.boxes && marker.kind==Kind::character && marker.halfHeight>0) {
            Vec3 head=marker.position,feet=marker.position;
            head.z+=marker.halfHeight;feet.z-=marker.halfHeight;
            nsport::Point top,bottom;
            if (project(head,snapshot->camera,(float)width,(float)height,top) &&
                project(feet,snapshot->camera,(float)width,(float)height,bottom)) {
                CGFloat boxHeight=fabs(bottom.y-top.y),boxWidth=boxHeight*0.45;
                if (boxHeight>2 && boxHeight<height*2)
                    CGContextStrokeRect(context,CGRectMake((top.x+bottom.x)/2-boxWidth/2,MIN(top.y,bottom.y),boxWidth,boxHeight));
            }
        }
        NSString *kind=marker.kind==Kind::character ? @"角色" : marker.kind==Kind::vehicle ? @"载具" : @"物资";
        if (self.names && !marker.playerName.empty()) {
            NSString *name=[[NSString alloc] initWithCharacters:(const unichar *)marker.playerName.data() length:marker.playerName.size()];
            if (name.length) kind=name;
        }
        NSMutableString *label=[kind mutableCopy];
        if (self.distances) [label appendFormat:@" %.0fm",distance(marker.position,snapshot->camera.position)/100];
        if (self.health && marker.hasHealth) [label appendFormat:@" %.0f/%.0f",marker.health,marker.maximum];
        [label drawAtPoint:CGPointMake(p.x+5,p.y+3) withAttributes:@{
            NSFontAttributeName:[UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightSemibold],
            NSForegroundColorAttributeName:color,
            NSBackgroundColorAttributeName:[UIColor.blackColor colorWithAlphaComponent:0.55]}];
    }
}
@end

@interface NSPViewController : UIViewController
@end
@implementation NSPViewController
- (BOOL)prefersStatusBarHidden {return YES;}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {return UIInterfaceOrientationMaskAll;}
@end

@interface NSPController : NSObject {
    std::unique_ptr<Sampler> _sampler;
    dispatch_queue_t _queue;
}
@property(nonatomic,strong) UIWindow *window;
@property(nonatomic,strong) NSPOverlay *overlay;
@property(nonatomic,strong) UIView *panel;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UIButton *button;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic,assign) BOOL enabled,busy;
@property(nonatomic,copy) NSString *lastStatus;
- (void)install:(UIWindowScene *)scene;
@end

@implementation NSPController
- (UIView *)switchRow:(NSString *)name selector:(SEL)selector on:(BOOL)on {
    UIView *row=[UIView new];
    [row.heightAnchor constraintEqualToConstant:36].active=YES;
    UILabel *label=[UILabel new];label.text=name;label.font=[UIFont systemFontOfSize:13];label.textColor=UIColor.whiteColor;
    UISwitch *toggle=[UISwitch new];toggle.on=on;
    [toggle addTarget:self action:selector forControlEvents:UIControlEventValueChanged];
    for (UIView *view in @[label,toggle]) {view.translatesAutoresizingMaskIntoConstraints=NO;[row addSubview:view];}
    [NSLayoutConstraint activateConstraints:@[
        [label.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:8],
        [label.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [toggle.trailingAnchor constraintEqualToAnchor:row.trailingAnchor constant:-8],
        [toggle.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [label.trailingAnchor constraintLessThanOrEqualToAnchor:toggle.leadingAnchor constant:-8]]];
    return row;
}
- (void)install:(UIWindowScene *)scene {
    _queue=dispatch_queue_create("org.newstate.port.sampling",DISPATCH_QUEUE_SERIAL);
    Memory memory{nullptr,NSPRead,(uint64_t)_dyld_get_image_header(0)};
    _sampler=std::make_unique<Sampler>(memory);
    self.window=[[UIWindow alloc] initWithWindowScene:scene];
    self.window.frame=scene.coordinateSpace.bounds;
    self.window.windowLevel=UIWindowLevelNormal+10;
    self.window.backgroundColor=UIColor.clearColor;
    NSPViewController *controller=[NSPViewController new];
    self.overlay=[[NSPOverlay alloc] initWithFrame:self.window.bounds];
    self.overlay.backgroundColor=UIColor.clearColor;self.overlay.opaque=NO;
    self.overlay.characters=YES;self.overlay.vehicles=YES;self.overlay.boxes=YES;self.overlay.health=YES;self.overlay.distances=YES;
    self.overlay.names=YES;self.overlay.crosshair=YES;
    controller.view=self.overlay;self.window.rootViewController=controller;
    self.button=[UIButton buttonWithType:UIButtonTypeSystem];
    [self.button setTitle:@"NSP" forState:UIControlStateNormal];
    self.button.backgroundColor=[UIColor.blackColor colorWithAlphaComponent:0.7];
    self.button.layer.cornerRadius=20;self.button.translatesAutoresizingMaskIntoConstraints=NO;
    [self.button addTarget:self action:@selector(togglePanel) forControlEvents:UIControlEventTouchUpInside];
    [self.overlay addSubview:self.button];
    [NSLayoutConstraint activateConstraints:@[
        [self.button.leadingAnchor constraintEqualToAnchor:self.overlay.safeAreaLayoutGuide.leadingAnchor constant:12],
        [self.button.topAnchor constraintEqualToAnchor:self.overlay.safeAreaLayoutGuide.topAnchor constant:12],
        [self.button.widthAnchor constraintEqualToConstant:48],[self.button.heightAnchor constraintEqualToConstant:40]]];
    self.panel=[UIView new];self.panel.backgroundColor=[UIColor colorWithWhite:0.08 alpha:0.94];
    self.panel.layer.cornerRadius=12;self.panel.hidden=YES;self.panel.translatesAutoresizingMaskIntoConstraints=NO;
    [self.overlay addSubview:self.panel];
    [NSLayoutConstraint activateConstraints:@[
        [self.panel.leadingAnchor constraintEqualToAnchor:self.button.leadingAnchor],
        [self.panel.topAnchor constraintEqualToAnchor:self.button.bottomAnchor constant:6],
        [self.panel.widthAnchor constraintEqualToConstant:310],
        [self.panel.bottomAnchor constraintEqualToAnchor:self.overlay.safeAreaLayoutGuide.bottomAnchor constant:-12]]];
    UIScrollView *scroll=[UIScrollView new];scroll.translatesAutoresizingMaskIntoConstraints=NO;[self.panel addSubview:scroll];
    UIStackView *stack=[UIStackView new];stack.axis=UILayoutConstraintAxisVertical;stack.spacing=5;stack.translatesAutoresizingMaskIntoConstraints=NO;
    [scroll addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:self.panel.topAnchor constant:8],
        [scroll.bottomAnchor constraintEqualToAnchor:self.panel.bottomAnchor constant:-8],
        [scroll.leadingAnchor constraintEqualToAnchor:self.panel.leadingAnchor constant:8],
        [scroll.trailingAnchor constraintEqualToAnchor:self.panel.trailingAnchor constant:-8],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor]]];
    self.status=[UILabel new];self.status.font=[UIFont systemFontOfSize:12];self.status.textColor=UIColor.whiteColor;self.status.numberOfLines=0;
    self.status.text=@"NEW STATE 0.9.98 · 只读适配 0.1.0\n关闭：点击开关开始采样";[stack addArrangedSubview:self.status];
    [stack addArrangedSubview:[self switchRow:@"开启只读显示" selector:@selector(enabledChanged:) on:NO]];
    [stack addArrangedSubview:[self switchRow:@"其他角色（包含队友）" selector:@selector(charactersChanged:) on:YES]];
    [stack addArrangedSubview:[self switchRow:@"载具" selector:@selector(vehiclesChanged:) on:YES]];
    [stack addArrangedSubview:[self switchRow:@"物资组／掉落物位置" selector:@selector(itemsChanged:) on:NO]];
    [stack addArrangedSubview:[self switchRow:@"胶囊轮廓方框" selector:@selector(boxesChanged:) on:YES]];
    [stack addArrangedSubview:[self switchRow:@"连线" selector:@selector(linesChanged:) on:NO]];
    [stack addArrangedSubview:[self switchRow:@"距离" selector:@selector(distancesChanged:) on:YES]];
    [stack addArrangedSubview:[self switchRow:@"角色血量" selector:@selector(healthChanged:) on:YES]];
    [stack addArrangedSubview:[self switchRow:@"角色姓名" selector:@selector(namesChanged:) on:YES]];
    [stack addArrangedSubview:[self switchRow:@"屏幕中心准星" selector:@selector(crosshairChanged:) on:YES]];
    UILabel *scope=[UILabel new];scope.text=@"尚未实现：队伍区分、骨架、自瞄、后坐力、去雾。实机效果待验证。";
    scope.numberOfLines=0;scope.font=[UIFont systemFontOfSize:11];scope.textColor=UIColor.lightGrayColor;[stack addArrangedSubview:scope];
    UIButton *share=[UIButton buttonWithType:UIButtonTypeSystem];[share setTitle:@"导出当前状态" forState:UIControlStateNormal];
    [share addTarget:self action:@selector(shareStatus) forControlEvents:UIControlEventTouchUpInside];[stack addArrangedSubview:share];
    self.window.hidden=NO;
    __weak NSPController *weakSelf=self;
    self.timer=[NSTimer scheduledTimerWithTimeInterval:0.2 repeats:YES block:^(NSTimer *timer) {
        (void)timer;NSPController *strongSelf=weakSelf;
        if (!strongSelf || !strongSelf.enabled || strongSelf.busy || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive) return;
        strongSelf.busy=YES;
        dispatch_async(strongSelf->_queue,^{
            @autoreleasepool {
                auto result=std::make_shared<Snapshot>();
                try {*result=strongSelf->_sampler->sample();} catch (...) {result->status="采样异常，已停止本次读取";}
                dispatch_async(dispatch_get_main_queue(),^{
                    strongSelf.busy=NO;
                    if (!strongSelf.enabled) return;
                    strongSelf.overlay.snapshot=result;
                    NSString *status=[NSString stringWithUTF8String:result->status.c_str()];
                    strongSelf.status.text=[NSString stringWithFormat:@"%@\n对象扫描 %u/%u · 标记 %zu",status,result->scanned,result->total,result->markers.size()];
                    if (![strongSelf.lastStatus isEqualToString:status]) {NSPLog(status);strongSelf.lastStatus=status;}
                    [strongSelf.overlay setNeedsDisplay];
                });
            }
        });
    }];
    NSPLog(@"overlay installed; memory sampling initially disabled");
}
- (void)togglePanel {self.panel.hidden=!self.panel.hidden;}
- (void)enabledChanged:(UISwitch *)sender {
    self.enabled=sender.on;
    if (!sender.on) {self.overlay.snapshot=nullptr;self.status.text=@"只读显示已关闭";[self.overlay setNeedsDisplay];}
}
- (void)charactersChanged:(UISwitch *)s {self.overlay.characters=s.on;[self.overlay setNeedsDisplay];}
- (void)vehiclesChanged:(UISwitch *)s {self.overlay.vehicles=s.on;[self.overlay setNeedsDisplay];}
- (void)itemsChanged:(UISwitch *)s {self.overlay.items=s.on;[self.overlay setNeedsDisplay];}
- (void)boxesChanged:(UISwitch *)s {self.overlay.boxes=s.on;[self.overlay setNeedsDisplay];}
- (void)linesChanged:(UISwitch *)s {self.overlay.lines=s.on;[self.overlay setNeedsDisplay];}
- (void)distancesChanged:(UISwitch *)s {self.overlay.distances=s.on;[self.overlay setNeedsDisplay];}
- (void)healthChanged:(UISwitch *)s {self.overlay.health=s.on;[self.overlay setNeedsDisplay];}
- (void)namesChanged:(UISwitch *)s {self.overlay.names=s.on;[self.overlay setNeedsDisplay];}
- (void)crosshairChanged:(UISwitch *)s {self.overlay.crosshair=s.on;[self.overlay setNeedsDisplay];}
- (void)shareStatus {
    NSMutableDictionary *report=[@{@"plugin_version":@"0.1.0",@"device_system":UIDevice.currentDevice.systemVersion,
        @"game_version":@"0.9.98",@"game_build":@"804",@"expected_uuid":NSPUUID,
        @"status":self.status.text?:@"",@"memory_writes":@NO,@"engine_calls":@NO} mutableCopy];
    auto snapshot=self.overlay.snapshot;
    if (snapshot) {
        report[@"camera_ready"]=@(snapshot->cameraReady);report[@"objects_scanned"]=@(snapshot->scanned);
        report[@"objects_total"]=@(snapshot->total);report[@"markers"]=@(snapshot->markers.size());
    }
    NSData *data=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
    NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:@"NewStatePort-status.json"];
    if (![data writeToFile:path atomically:YES]) return;
    UIActivityViewController *share=[[UIActivityViewController alloc] initWithActivityItems:@[[NSURL fileURLWithPath:path]] applicationActivities:nil];
    share.popoverPresentationController.sourceView=self.button;
    [self.window.rootViewController presentViewController:share animated:YES completion:nil];
}
@end

static NSPController *NSPInstance;
static void NSPInstall(void) {
    if (NSPInstance) return;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if ([scene isKindOfClass:UIWindowScene.class] && scene.activationState==UISceneActivationStateForegroundActive) {
            NSPInstance=[NSPController new];[NSPInstance install:(UIWindowScene *)scene];return;
        }
    }
}
__attribute__((constructor)) static void NSPEntry(void) {
    @autoreleasepool {
        if (![NSBundle.mainBundle.bundleIdentifier isEqualToString:@"com.pubg.newstate"]) return;
        NSString *reason=NSPValidate();
        NSPLog([NSString stringWithFormat:@"NewStatePort 0.1.0 loaded; iOS %@; %@",UIDevice.currentDevice.systemVersion,reason?:@"target identity matched"]);
        if (reason) return;
        dispatch_async(dispatch_get_main_queue(),^{
            [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidBecomeActiveNotification object:nil
                queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {(void)notification;NSPInstall();}];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{NSPInstall();});
        });
    }
}
