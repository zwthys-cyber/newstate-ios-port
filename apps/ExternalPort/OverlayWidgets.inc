// Own overlay controls only. OS touch observation follows TrollSpeed's AX bridge (MIT).
static BOOL NSPTouchBridgeConfigured=NO;
static CGPoint NSPBoundCenter(CGPoint p,CGSize size,CGSize bounds) {
    CGFloat mx=MIN(bounds.width/2,size.width/2+12),my=MIN(bounds.height/2,size.height/2+12);
    return CGPointMake(MAX(mx,MIN(bounds.width-mx,p.x)),MAX(my,MIN(bounds.height-my,p.y)));
}
@interface NSPRadarView : UIView
@property(nonatomic) std::shared_ptr<nsport::Snapshot> snapshot;
@property(nonatomic,copy) NSDictionary *options;
@property(nonatomic) BOOL editing;
@end
@implementation NSPRadarView
- (void)drawRect:(CGRect)rect {
    (void)rect;CGContextRef c=UIGraphicsGetCurrentContext();if (!c) return;
    CGFloat w=self.bounds.size.width,h=self.bounds.size.height,r=MIN(w,h)/2-18;
    CGPoint center=CGPointMake(w/2,h/2-3);
    CGContextSetFillColorWithColor(c,[UIColor.blackColor colorWithAlphaComponent:0.65].CGColor);
    CGContextFillEllipseInRect(c,CGRectMake(center.x-r,center.y-r,2*r,2*r));
    CGContextSetLineWidth(c,1);CGContextSetStrokeColorWithColor(c,[UIColor.whiteColor colorWithAlphaComponent:0.35].CGColor);
    for (CGFloat scale:{0.5,1.0}) CGContextStrokeEllipseInRect(c,CGRectMake(center.x-r*scale,center.y-r*scale,2*r*scale,2*r*scale));
    CGContextMoveToPoint(c,center.x-r,center.y);CGContextAddLineToPoint(c,center.x+r,center.y);
    CGContextMoveToPoint(c,center.x,center.y-r);CGContextAddLineToPoint(c,center.x,center.y+r);CGContextStrokePath(c);
    CGFloat range=[self.options[@"radar_range"] doubleValue];NSUInteger count=0;
    BOOL ready=[self.options[@"sampling"] boolValue] && self.snapshot && self.snapshot->cameraReady;
    if (ready) for (const auto &marker:self.snapshot->markers) {
        if (marker.kind!=nsport::Kind::character ||
            (marker.relation==nsport::Relation::teammate && [self.options[@"hide_teammates"] boolValue])) continue;
        nsport::Point p;if (!nsport::radarOffset(marker.position,self.snapshot->camera,(float)range,p)) continue;
        CGContextSetFillColorWithColor(c,NSPCharacterColor(marker).CGColor);
        CGContextFillEllipseInRect(c,CGRectMake(center.x+p.x*r-2.5,center.y+p.y*r-2.5,5,5));++count;
    }
    CGContextSetFillColorWithColor(c,UIColor.whiteColor.CGColor);
    CGContextMoveToPoint(c,center.x,center.y-6);CGContextAddLineToPoint(c,center.x-4,center.y+4);
    CGContextAddLineToPoint(c,center.x+4,center.y+4);CGContextClosePath(c);CGContextFillPath(c);
    NSDictionary *text=@{NSFontAttributeName:[UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightSemibold],NSForegroundColorAttributeName:UIColor.whiteColor};
    NSString *title=self.editing ? @"拖动雷达" : @"前方";
    [title drawAtPoint:CGPointMake((w-[title sizeWithAttributes:text].width)/2,1) withAttributes:text];
    NSString *footer=ready ? [NSString stringWithFormat:@"%.0fm · %lu",range,(unsigned long)count] : @"等待数据";
    [footer drawAtPoint:CGPointMake((w-[footer sizeWithAttributes:text].width)/2,h-14) withAttributes:text];
    if (self.editing) {CGContextSetStrokeColorWithColor(c,UIColor.systemTealColor.CGColor);CGContextStrokeRect(c,CGRectInset(self.bounds,1,1));}
}
@end
@interface NSPFloatButton : UIButton
@property(nonatomic,copy) void (^activate)(void);
@end
@implementation NSPFloatButton
- (BOOL)accessibilityActivate {if (self.activate) self.activate();return YES;}
@end
@interface NSPWidgetLayer : UIView {
    NSInteger _activeIdentity,_activeTarget;
    CGPoint _touchStart,_centerStart;
    NSTimeInterval _touchTime,_layoutRead;
    BOOL _moved;
}
@property(nonatomic,weak) NSPCanvas *canvas;
@property(nonatomic,strong) NSPRadarView *radar;
@property(nonatomic,strong) NSPFloatButton *button;
@property(nonatomic,copy) NSDictionary *options;
@property(nonatomic,strong) NSMutableDictionary *placement;
@property(nonatomic) BOOL collapsed,editing;
@property(nonatomic) NSUInteger interactions;
- (void)updateWithOptions:(NSDictionary *)options;
- (void)refreshRadar;
- (void)activateButton;
- (void)handlePoint:(CGPoint)point phase:(UITouchPhase)phase identity:(NSInteger)identity;
@end
static __weak NSPWidgetLayer *NSPActiveWidgets;
@implementation NSPWidgetLayer
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self=[super initWithFrame:frame])) {
        self.backgroundColor=UIColor.clearColor;_activeIdentity=-1;self.placement=[NSMutableDictionary dictionary];
        self.radar=[NSPRadarView new];self.radar.backgroundColor=UIColor.clearColor;self.radar.opaque=NO;[self addSubview:self.radar];
        self.button=[NSPFloatButton buttonWithType:UIButtonTypeCustom];self.button.backgroundColor=[UIColor colorWithWhite:0.08 alpha:0.9];
        self.button.layer.cornerRadius=22;self.button.layer.borderWidth=1;self.button.layer.borderColor=UIColor.systemTealColor.CGColor;
        self.button.titleLabel.font=[UIFont boldSystemFontOfSize:11];[self.button setTitleColor:UIColor.systemTealColor forState:UIControlStateNormal];
        self.button.accessibilityLabel=@"收起或恢复标记";self.button.accessibilityHint=@"拖动按钮；长按后松开可解锁雷达位置，再长按锁定";
        __weak NSPWidgetLayer *weak=self;self.button.activate=^{[weak activateButton];};[self addSubview:self.button];
    }return self;
}
- (CGFloat)coordinate:(NSString *)key fallback:(CGFloat)fallback {
    id v=self.placement[key];if (![v isKindOfClass:NSNumber.class] || !std::isfinite([v doubleValue])) return fallback;
    return MAX(0,MIN(1,[v doubleValue]));
}
- (void)layoutSubviews {
    [super layoutSubviews];CGSize b=self.bounds.size;self.canvas.frame=self.bounds;
    CGFloat side=MAX(100,MIN(200,[self.options[@"radar_size"] doubleValue]));
    self.radar.bounds=CGRectMake(0,0,side,side);self.button.bounds=CGRectMake(0,0,44,44);
    if (_activeTarget!=1) self.button.center=NSPBoundCenter(CGPointMake(b.width*[self coordinate:@"button_x" fallback:0.055],b.height*[self coordinate:@"button_y" fallback:0.5]),self.button.bounds.size,b);
    if (_activeTarget!=2) self.radar.center=NSPBoundCenter(CGPointMake(b.width*[self coordinate:@"radar_x" fallback:0.2],b.height*[self coordinate:@"radar_y" fallback:0.28]),self.radar.bounds.size,b);
}
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    if (self.hidden || self.alpha<0.01) return nil;
    if (!self.button.hidden && CGRectContainsPoint(self.button.frame,point)) return [self.button hitTest:[self.button convertPoint:point fromView:self] withEvent:event];
    if (self.editing && !self.radar.hidden && CGRectContainsPoint(self.radar.frame,point)) return self.radar;
    return nil;
}
- (void)updateWithOptions:(NSDictionary *)options {
    self.options=options;NSTimeInterval now=NSProcessInfo.processInfo.systemUptime;
    if (_activeIdentity<0 && now-_layoutRead>0.25) {self.placement=[NSPReadJSON(@"overlay-layout.json") mutableCopy]?:[NSMutableDictionary dictionary];_layoutRead=now;}
    self.button.hidden=![options[@"floating_button"] boolValue];if (self.button.hidden) self.editing=NO;
    self.canvas.hidden=self.collapsed;self.radar.hidden=self.collapsed || ![options[@"radar"] boolValue];
    self.radar.options=options;self.radar.editing=self.editing;
    self.radar.alpha=MAX(0.35,MIN(1,[options[@"overlay_opacity"] doubleValue]));
    [self.button setTitle:self.editing ? @"定位" : self.collapsed ? @"显示" : @"NSP" forState:UIControlStateNormal];
    [self setNeedsLayout];[self layoutIfNeeded];[self refreshRadar];
}
- (void)refreshRadar {self.radar.snapshot=self.canvas.snapshot;[self.radar setNeedsDisplay];}
- (void)activateButton {
    self.collapsed=!self.collapsed;self.editing=NO;++self.interactions;[self updateWithOptions:self.options];
}
- (void)handlePoint:(CGPoint)point phase:(UITouchPhase)phase identity:(NSInteger)identity {
    if (phase==UITouchPhaseBegan) {
        if (_activeIdentity>=0 || identity<0) return;
        UIView *hit=[self hitTest:point withEvent:nil];
        NSInteger target=hit==self.button ? 1 : hit==self.radar ? 2 : 0;if (!target) return;
        _activeIdentity=identity;_activeTarget=target;_touchStart=point;_centerStart=target==1 ? self.button.center : self.radar.center;
        _touchTime=NSProcessInfo.processInfo.systemUptime;_moved=NO;return;
    }
    if (_activeIdentity!=identity || !_activeTarget) return;
    UIView *target=_activeTarget==1 ? self.button : self.radar;
    CGFloat dx=point.x-_touchStart.x,dy=point.y-_touchStart.y;
    if (phase==UITouchPhaseMoved) {
        if (std::hypot(dx,dy)>8) _moved=YES;
        if (_moved) target.center=NSPBoundCenter(CGPointMake(_centerStart.x+dx,_centerStart.y+dy),target.bounds.size,self.bounds.size);
        return;
    }
    if (phase!=UITouchPhaseEnded && phase!=UITouchPhaseCancelled) return;
    NSInteger active=_activeTarget;_activeTarget=0;_activeIdentity=-1;
    if (phase==UITouchPhaseCancelled) {target.center=_centerStart;return;}
    ++self.interactions;
    if (_moved && self.bounds.size.width>0 && self.bounds.size.height>0) {
        NSString *prefix=active==1 ? @"button" : @"radar";
        self.placement[[prefix stringByAppendingString:@"_x"]]=@(target.center.x/self.bounds.size.width);
        self.placement[[prefix stringByAppendingString:@"_y"]]=@(target.center.y/self.bounds.size.height);
        NSPWriteJSON(@"overlay-layout.json",self.placement);
    } else if (active==1) {
        if (NSProcessInfo.processInfo.systemUptime-_touchTime>=0.6) {self.editing=!self.editing;self.collapsed=NO;}
        else {self.collapsed=!self.collapsed;self.editing=NO;}
    }
    [self updateWithOptions:self.options];
}
@end
// Receive OS AX touch observations; never synthesize touches or control the game.
@interface NSPAXRepresentation : NSObject
+ (instancetype)representationWithHIDEvent:(CFTypeRef)event hidStreamIdentifier:(NSString *)identifier;
- (CGPoint)location;
- (BOOL)isTouchDown;
- (BOOL)isMove;
- (BOOL)isCancel;
- (BOOL)isLift;
- (BOOL)isInRangeLift;
- (id)handInfo;
@end
static Class NSPAXClass;
[[maybe_unused]] static void NSPOverlayTouch(void *target,void *refcon,void *service,CFTypeRef event) {
    (void)target;(void)refcon;(void)service;
    @try {
        NSPAXRepresentation *rep=[NSPAXClass representationWithHIDEvent:event hidStreamIdentifier:@"UIApplicationEvents"];
        NSArray *paths=[[rep handInfo] valueForKey:@"paths"];
        if (![paths isKindOfClass:NSArray.class] || !paths.count) return;
        NSNumber *identity=[paths.firstObject valueForKey:@"pathIdentity"];if (![identity isKindOfClass:NSNumber.class]) return;
        UITouchPhase phase;
        if ([rep isCancel]) phase=UITouchPhaseCancelled;
        else if ([rep isTouchDown]) phase=UITouchPhaseBegan;
        else if ([rep isMove]) phase=UITouchPhaseMoved;
        else if ([rep isLift] || [rep isInRangeLift]) phase=UITouchPhaseEnded;
        else return;
        CGPoint point=[rep location];NSInteger touchID=identity.integerValue;
        dispatch_async(dispatch_get_main_queue(),^{
            NSPWidgetLayer *widgets=NSPActiveWidgets;if (!widgets.window) return;
            [widgets handlePoint:[widgets convertPoint:point fromView:widgets.window] phase:phase identity:touchID];
        });
    } @catch (__unused NSException *exception) {}
}
#ifdef NSP_UI_PREVIEW
@interface NSPWidgetPreviewController : UIViewController
@property(nonatomic,strong) NSPWidgetLayer *widgets;
@property(nonatomic,strong) NSPCanvas *canvas;
@property(nonatomic,strong) UILabel *caption;
@end
@implementation NSPWidgetPreviewController
- (void)viewDidLoad {
    [super viewDidLoad];self.view.backgroundColor=[UIColor colorWithRed:0.035 green:0.05 blue:0.07 alpha:1];
    self.widgets=[NSPWidgetLayer new];self.canvas=[NSPCanvas new];self.canvas.backgroundColor=UIColor.clearColor;self.canvas.opaque=NO;
    self.widgets.canvas=self.canvas;[self.widgets insertSubview:self.canvas atIndex:0];[self.view addSubview:self.widgets];
    NSMutableDictionary *options=[NSPDefaults() mutableCopy];options[@"crosshair"]=@NO;self.canvas.options=options;[self.widgets updateWithOptions:options];
    self.caption=[UILabel new];self.caption.text=@"二维雷达与悬浮按钮\n无游戏数据预览";self.caption.numberOfLines=2;
    self.caption.font=[UIFont boldSystemFontOfSize:20];self.caption.textColor=UIColor.systemTealColor;self.caption.textAlignment=NSTextAlignmentCenter;
    [self.view addSubview:self.caption];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];self.widgets.frame=self.view.bounds;[self.widgets setNeedsLayout];[self.widgets layoutIfNeeded];
    self.caption.frame=CGRectMake(20,110,MAX(0,self.view.bounds.size.width-40),70);
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/2),dispatch_get_main_queue(),^{
        NSPWriteJSON(@"ui-ready.json",@{@"widgets":@YES});
    });
}
@end
#endif
