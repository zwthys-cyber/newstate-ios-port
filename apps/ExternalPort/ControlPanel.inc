// Native controller presentation, shared by the device app and simulator preview.
// Runtime process actions remain in main.mm; this file never supplies game data.
static UIColor *NSPAccent(void) {return [UIColor colorWithRed:0.25 green:0.88 blue:0.79 alpha:1];}
static UIColor *NSPBackground(void) {return [UIColor colorWithRed:0.035 green:0.055 blue:0.085 alpha:1];}
static UIColor *NSPCardColor(void) {return [UIColor colorWithRed:0.075 green:0.10 blue:0.14 alpha:1];}

@interface NSPControl : UIViewController
@property(nonatomic,strong) NSMutableDictionary *settings;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic) BOOL checkingMemory;
@property(nonatomic) BOOL startingHUD;
@property(nonatomic) NSUInteger launchGeneration;
@property(nonatomic,strong) UISegmentedControl *pages;
@property(nonatomic,strong) UIScrollView *scroll;
@property(nonatomic,strong) UIStackView *content;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UILabel *statusDetail;
@property(nonatomic,strong) UILabel *metrics;
@property(nonatomic,strong) UILabel *memoryStatus;
@property(nonatomic,strong) UIView *statusDot;
@property(nonatomic,strong) UIButton *startButton;
@property(nonatomic,strong) UIButton *memoryButton;
@property(nonatomic,strong) NSMutableDictionary<NSString *,UISwitch *> *toggles;
@property(nonatomic,strong) NSMutableDictionary<NSString *,UILabel *> *values;
@property(nonatomic,strong) NSMutableDictionary<NSString *,UISlider *> *sliders;
@property(nonatomic,strong) UISegmentedControl *presets;
- (void)save;
- (void)refresh;
@end
@interface NSPControl (Runtime)
- (void)startHUD;
- (void)stopHUD;
- (void)spawnHUD;
- (void)checkMemory;
- (void)shareStatus;
@end

@implementation NSPControl
- (UILabel *)label:(NSString *)text size:(CGFloat)size weight:(UIFontWeight)weight color:(UIColor *)color {
    UILabel *label=[UILabel new];label.text=text;label.numberOfLines=0;label.textColor=color;
    UIFont *font=[UIFont systemFontOfSize:size weight:weight];
    label.font=[[UIFontMetrics metricsForTextStyle:UIFontTextStyleBody] scaledFontForFont:font maximumPointSize:size*1.5];
    label.adjustsFontForContentSizeCategory=YES;return label;
}
- (UIStackView *)vertical:(CGFloat)spacing {
    UIStackView *stack=[UIStackView new];stack.axis=UILayoutConstraintAxisVertical;stack.spacing=spacing;return stack;
}
- (UIStackView *)card:(NSString *)title subtitle:(NSString *)subtitle {
    if (title.length) {
        UILabel *heading=[self label:title size:13 weight:UIFontWeightSemibold color:UIColor.secondaryLabelColor];
        heading.accessibilityTraits|=UIAccessibilityTraitHeader;
        [self.content addArrangedSubview:heading];
    }
    UIStackView *card=[self vertical:0];card.backgroundColor=NSPCardColor();card.layer.cornerRadius=20;
    card.layoutMargins=UIEdgeInsetsMake(8,16,8,16);card.layoutMarginsRelativeArrangement=YES;
    [self.content addArrangedSubview:card];
    if (subtitle.length) {
        UILabel *note=[self label:subtitle size:12 weight:UIFontWeightRegular color:UIColor.secondaryLabelColor];
        [self.content addArrangedSubview:note];
    }
    return card;
}
- (void)separator:(UIStackView *)card {
    UIView *line=[UIView new];line.backgroundColor=[UIColor.whiteColor colorWithAlphaComponent:0.07];
    [line.heightAnchor constraintEqualToConstant:0.5].active=YES;[card addArrangedSubview:line];
}
- (UIButton *)button:(NSString *)title symbol:(NSString *)symbol action:(SEL)action primary:(BOOL)primary {
    UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *config=primary ? UIButtonConfiguration.filledButtonConfiguration : UIButtonConfiguration.tintedButtonConfiguration;
    config.title=title;config.image=[UIImage systemImageNamed:symbol];config.imagePadding=8;
    config.baseBackgroundColor=NSPAccent();config.baseForegroundColor=primary ? NSPBackground() : NSPAccent();
    config.cornerStyle=UIButtonConfigurationCornerStyleLarge;
    config.contentInsets=NSDirectionalEdgeInsetsMake(14,16,14,16);button.configuration=config;
    button.titleLabel.adjustsFontForContentSizeCategory=YES;
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:50].active=YES;
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];return button;
}
- (void)toggleRow:(NSString *)key title:(NSString *)title detail:(NSString *)detail card:(UIStackView *)card {
    UIStackView *copy=[self vertical:3];[copy addArrangedSubview:[self label:title size:15 weight:UIFontWeightMedium color:UIColor.labelColor]];
    if (detail.length) [copy addArrangedSubview:[self label:detail size:12 weight:UIFontWeightRegular color:UIColor.secondaryLabelColor]];
    UISwitch *toggle=[UISwitch new];toggle.onTintColor=NSPAccent();toggle.on=[self.settings[key] boolValue];
    toggle.accessibilityIdentifier=key;toggle.accessibilityLabel=title;toggle.accessibilityHint=detail;
    [toggle setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [toggle setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [toggle addTarget:self action:@selector(toggle:) forControlEvents:UIControlEventValueChanged];self.toggles[key]=toggle;
    UIStackView *row=[[UIStackView alloc] initWithArrangedSubviews:@[copy,toggle]];row.axis=UILayoutConstraintAxisHorizontal;
    row.spacing=12;row.alignment=UIStackViewAlignmentCenter;row.layoutMargins=UIEdgeInsetsMake(12,0,12,0);
    row.layoutMarginsRelativeArrangement=YES;[row.heightAnchor constraintGreaterThanOrEqualToConstant:56].active=YES;
    [card addArrangedSubview:row];
}
- (void)sliderRow:(NSString *)key title:(NSString *)title minimum:(float)minimum maximum:(float)maximum card:(UIStackView *)card {
    UIStackView *row=[self vertical:6];row.layoutMargins=UIEdgeInsetsMake(12,0,12,0);row.layoutMarginsRelativeArrangement=YES;
    UILabel *name=[self label:title size:15 weight:UIFontWeightMedium color:UIColor.labelColor];
    UILabel *value=[self label:@"" size:13 weight:UIFontWeightSemibold color:NSPAccent()];value.textAlignment=NSTextAlignmentRight;
    UIStackView *heading=[[UIStackView alloc] initWithArrangedSubviews:@[name,value]];heading.axis=UILayoutConstraintAxisHorizontal;
    [row addArrangedSubview:heading];self.values[key]=value;
    UISlider *slider=[UISlider new];slider.minimumValue=minimum;slider.maximumValue=maximum;
    slider.value=[self.settings[key] floatValue];slider.minimumTrackTintColor=NSPAccent();slider.accessibilityIdentifier=key;
    slider.accessibilityLabel=title;[slider.heightAnchor constraintGreaterThanOrEqualToConstant:44].active=YES;
    [slider addTarget:self action:@selector(sliderChanged:) forControlEvents:UIControlEventValueChanged];
    self.sliders[key]=slider;[row addArrangedSubview:slider];[card addArrangedSubview:row];[self updateSliderValue:key];
}
- (void)viewDidLoad {
    [super viewDidLoad];self.title=@"NSP Pro";self.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;
    self.view.backgroundColor=NSPBackground();self.settings=[NSPSettings() mutableCopy];
    UINavigationBarAppearance *appearance=[UINavigationBarAppearance new];[appearance configureWithOpaqueBackground];
    appearance.backgroundColor=NSPBackground();appearance.shadowColor=UIColor.clearColor;
    appearance.titleTextAttributes=@{NSForegroundColorAttributeName:UIColor.whiteColor};
    self.navigationController.navigationBar.standardAppearance=appearance;
    self.navigationController.navigationBar.scrollEdgeAppearance=appearance;self.navigationController.navigationBar.tintColor=NSPAccent();
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"square.and.arrow.up"]
        style:UIBarButtonItemStylePlain target:self action:@selector(shareStatus)];
    self.navigationItem.rightBarButtonItem.accessibilityLabel=@"导出诊断";
    self.pages=[[UISegmentedControl alloc] initWithItems:@[@"总览",@"显示",@"工具"]];self.pages.selectedSegmentIndex=0;
    self.pages.selectedSegmentTintColor=[NSPAccent() colorWithAlphaComponent:0.22];self.pages.translatesAutoresizingMaskIntoConstraints=NO;
    [self.pages addTarget:self action:@selector(pageChanged:) forControlEvents:UIControlEventValueChanged];[self.view addSubview:self.pages];
    self.scroll=[UIScrollView new];self.scroll.translatesAutoresizingMaskIntoConstraints=NO;self.scroll.alwaysBounceVertical=YES;
    [self.view addSubview:self.scroll];self.content=[self vertical:14];self.content.translatesAutoresizingMaskIntoConstraints=NO;[self.scroll addSubview:self.content];
    UILayoutGuide *safe=self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [self.pages.topAnchor constraintEqualToAnchor:safe.topAnchor constant:8],
        [self.pages.leadingAnchor constraintEqualToAnchor:self.content.leadingAnchor],
        [self.pages.trailingAnchor constraintEqualToAnchor:self.content.trailingAnchor],
        [self.pages.heightAnchor constraintGreaterThanOrEqualToConstant:36],
        [self.scroll.topAnchor constraintEqualToAnchor:self.pages.bottomAnchor constant:10],
        [self.scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [self.scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [self.scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],
        [self.scroll.contentLayoutGuide.widthAnchor constraintEqualToAnchor:self.scroll.frameLayoutGuide.widthAnchor],
        [self.content.topAnchor constraintEqualToAnchor:self.scroll.contentLayoutGuide.topAnchor constant:12],
        [self.content.bottomAnchor constraintEqualToAnchor:self.scroll.contentLayoutGuide.bottomAnchor constant:-28],
        [self.content.centerXAnchor constraintEqualToAnchor:self.scroll.frameLayoutGuide.centerXAnchor],
        [self.content.widthAnchor constraintLessThanOrEqualToConstant:620],
        [self.content.widthAnchor constraintLessThanOrEqualToAnchor:self.scroll.frameLayoutGuide.widthAnchor constant:-36]]];
    NSLayoutConstraint *width=[self.content.widthAnchor constraintEqualToAnchor:self.scroll.frameLayoutGuide.widthAnchor constant:-36];
    width.priority=UILayoutPriorityDefaultHigh;width.active=YES;
#ifdef NSP_UI_PREVIEW
    NSArray *arguments=NSProcessInfo.processInfo.arguments;NSUInteger index=[arguments indexOfObject:@"--nsp-page"];
    if (index!=NSNotFound && index+1<arguments.count) self.pages.selectedSegmentIndex=[arguments[index+1] integerValue];
#endif
    [self renderPage];__weak NSPControl *weak=self;
    self.timer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {(void)timer;[weak refresh];}];
    [self save];[self refresh];
#ifdef NSP_UI_PREVIEW
    if ([NSProcessInfo.processInfo.arguments containsObject:@"--nsp-smoke"])
        dispatch_async(dispatch_get_main_queue(),^{[self runPreviewChecks];});
#endif
}
- (void)renderPage {
    for (UIView *view in self.content.arrangedSubviews) {[self.content removeArrangedSubview:view];[view removeFromSuperview];}
    self.toggles=[NSMutableDictionary dictionary];self.values=[NSMutableDictionary dictionary];self.sliders=[NSMutableDictionary dictionary];
    self.status=nil;self.statusDetail=nil;self.metrics=nil;self.statusDot=nil;self.startButton=nil;self.presets=nil;
    self.memoryStatus=nil;self.memoryButton=nil;
    if (self.pages.selectedSegmentIndex==1) [self displayPage];
    else if (self.pages.selectedSegmentIndex==2) [self toolsPage];
    else [self overviewPage];
    [self.scroll setContentOffset:CGPointZero animated:NO];[self refresh];
}
#ifdef NSP_UI_PREVIEW
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/2),dispatch_get_main_queue(),^{
        [self.view layoutIfNeeded];
        NSPWriteJSON(@"ui-ready.json",@{@"page":@(self.pages.selectedSegmentIndex),@"time":@(NSDate.date.timeIntervalSince1970)});
    });
}
#endif
- (void)pageChanged:(UISegmentedControl *)sender {(void)sender;[self renderPage];}
- (void)overviewPage {
    UIStackView *hero=[self vertical:6];hero.layoutMargins=UIEdgeInsetsMake(8,2,10,2);hero.layoutMarginsRelativeArrangement=YES;
    [hero addArrangedSubview:[self label:@"EXTERNAL DISPLAY  /  PRO" size:10 weight:UIFontWeightBold color:NSPAccent()]];
    [hero addArrangedSubview:[self label:@"把视野留给游戏。" size:30 weight:UIFontWeightBold color:UIColor.whiteColor]];
    [hero addArrangedSubview:[self label:@"NEW STATE 0.9.98 · iOS 17.0 · TrollStore" size:12 weight:UIFontWeightMedium color:UIColor.secondaryLabelColor]];
    [self.content addArrangedSubview:hero];
    UIStackView *session=[self card:nil subtitle:nil];session.spacing=12;session.layoutMargins=UIEdgeInsetsMake(18,18,18,18);
    self.statusDot=[UIView new];self.statusDot.layer.cornerRadius=4;
    [self.statusDot.widthAnchor constraintEqualToConstant:8].active=YES;[self.statusDot.heightAnchor constraintEqualToConstant:8].active=YES;
    self.status=[self label:@"尚未启动" size:18 weight:UIFontWeightSemibold color:UIColor.whiteColor];
    UIStackView *state=[[UIStackView alloc] initWithArrangedSubviews:@[self.statusDot,self.status]];state.axis=UILayoutConstraintAxisHorizontal;
    state.spacing=10;state.alignment=UIStackViewAlignmentCenter;[session addArrangedSubview:state];
    self.statusDetail=[self label:@"" size:13 weight:UIFontWeightRegular color:UIColor.secondaryLabelColor];[session addArrangedSubview:self.statusDetail];
    self.metrics=[self label:@"" size:12 weight:UIFontWeightMedium color:NSPAccent()];[session addArrangedSubview:self.metrics];
    self.startButton=[self button:@"启动悬浮层" symbol:@"play.fill" action:@selector(startHUD) primary:YES];[session addArrangedSubview:self.startButton];
    [session addArrangedSubview:[self button:@"停止悬浮层" symbol:@"stop.fill" action:@selector(stopHUD) primary:NO]];
    UIStackView *reading=[self card:@"快速控制" subtitle:@"先启动悬浮层，再打开原版游戏。显示设置即时保存。"];
    [self toggleRow:@"sampling" title:@"游戏读取" detail:@"开启后显示角色、载具及所选标记" card:reading];
    [self separator:reading];[self toggleRow:@"hud_status" title:@"屏幕状态文字" detail:@"默认关闭，减少视野遮挡" card:reading];
    UIStackView *guide=[self card:@"当前配置" subtitle:nil];
    [guide addArrangedSubview:[self label:@"已确认队友默认隐藏\n真人敌人红色 · 人机黄色 · 未知橙色\n骨架建议保持关闭。" size:13 weight:UIFontWeightRegular color:UIColor.secondaryLabelColor]];
    [guide addArrangedSubview:[self label:[NSString stringWithFormat:@"NSP Pro %@  ·  保持开发者模式关闭",NSPVersion]
        size:11 weight:UIFontWeightMedium color:UIColor.tertiaryLabelColor]];
}
- (void)displayPage {
    [self.content addArrangedSubview:[self label:@"你的显示偏好" size:27 weight:UIFontWeightBold color:UIColor.whiteColor]];
    [self.content addArrangedSubview:[self label:@"预设快速开始，每一项仍可独立调整。" size:13 weight:UIFontWeightRegular color:UIColor.secondaryLabelColor]];
    UIStackView *preset=[self card:@"显示预设" subtitle:@"预设只调整标记内容，不会开启游戏读取，也不会启用骨架。"];
    self.presets=[[UISegmentedControl alloc] initWithItems:@[@"简洁",@"均衡",@"详细"]];
    self.presets.accessibilityLabel=@"显示预设";self.presets.selectedSegmentIndex=UISegmentedControlNoSegment;
    [self.presets.heightAnchor constraintGreaterThanOrEqualToConstant:40].active=YES;
    [self.presets addTarget:self action:@selector(presetChanged:) forControlEvents:UIControlEventValueChanged];[preset addArrangedSubview:self.presets];
    UIStackView *targets=[self card:@"目标类型" subtitle:@"真人敌人红色，人机黄色。队伍或类型尚未确认时使用橙色，避免误认。"];
    [self toggleRow:@"characters" title:@"角色" detail:nil card:targets];[self separator:targets];
    [self toggleRow:@"hide_teammates" title:@"隐藏队友" detail:@"确认同队后隐藏全部标记" card:targets];[self separator:targets];
    [self toggleRow:@"vehicles" title:@"载具" detail:nil card:targets];[self separator:targets];
    [self toggleRow:@"airdrops" title:@"空投" detail:@"紫色标记，独立于地面物资" card:targets];[self separator:targets];
    [self toggleRow:@"items" title:@"物资" detail:@"实验性" card:targets];
    UIStackView *labels=[self card:@"标记信息" subtitle:nil];
    for (NSArray *entry in @[@[@"names",@"姓名"],@[@"distances",@"距离"],@[@"health",@"血量数值"],@[@"health_bars",@"血量条"]]) {
        if (labels.arrangedSubviews.count) [self separator:labels];
        [self toggleRow:entry[0] title:entry[1] detail:nil card:labels];
    }
    UIStackView *geometry=[self card:@"辅助绘制" subtitle:@"连线从屏幕顶部中央指向目标；骨架是身体关节线，当前可能包含多余分支。"];
    for (NSArray *entry in @[@[@"boxes",@"人物方框"],@[@"lines",@"目标连线"],@[@"edge_indicators",@"屏外方向"],
        @[@"crosshair",@"中心准星"],@[@"skeletons",@"骨架 · 实验性"]]) {
        if (geometry.arrangedSubviews.count) [self separator:geometry];
        [self toggleRow:entry[0] title:entry[1] detail:nil card:geometry];
    }
    UIStackView *style=[self card:@"视觉样式" subtitle:@"调节应用到悬浮层；不会改变游戏内画面。"];
    [self sliderRow:@"label_scale" title:@"文字大小" minimum:0.8f maximum:1.3f card:style];[self separator:style];
    [self sliderRow:@"overlay_opacity" title:@"显示不透明度" minimum:0.35f maximum:1.0f card:style];[self separator:style];
    [self sliderRow:@"line_width" title:@"线条粗细" minimum:0.6f maximum:2.0f card:style];
    UIStackView *direction=[self card:@"屏幕方向" subtitle:@"方向需要手动选择，选择与你的游戏画面一致的方向。"];
    UISegmentedControl *orientation=[[UISegmentedControl alloc] initWithItems:@[@"横屏左",@"横屏右",@"竖屏"]];
    orientation.selectedSegmentIndex=MAX(0,MIN(2,[self.settings[@"orientation"] integerValue]));orientation.accessibilityLabel=@"悬浮层方向";
    [orientation.heightAnchor constraintGreaterThanOrEqualToConstant:40].active=YES;
    [orientation addTarget:self action:@selector(orientationChanged:) forControlEvents:UIControlEventValueChanged];[direction addArrangedSubview:orientation];
}
- (void)toolsPage {
    [self.content addArrangedSubview:[self label:@"工具与支持" size:27 weight:UIFontWeightBold color:UIColor.whiteColor]];
    [self.content addArrangedSubview:[self label:@"查看实际状态，保留可追溯的诊断记录。" size:13 weight:UIFontWeightRegular color:UIColor.secondaryLabelColor]];
    UIStackView *diagnostics=[self card:@"运行诊断" subtitle:nil];diagnostics.spacing=10;
    [diagnostics addArrangedSubview:[self button:@"查看诊断详情" symbol:@"waveform.path.ecg" action:@selector(showDiagnostics) primary:NO]];
    [diagnostics addArrangedSubview:[self button:@"导出状态文件" symbol:@"square.and.arrow.up" action:@selector(shareStatus) primary:NO]];
    UIStackView *memory=[self card:@"内存权限验证" subtitle:@"仅手动运行。测试本工具自有子进程，不写游戏数据；正常退出不等于取得控制端口。"];
    memory.spacing=12;self.memoryStatus=[self label:@"尚无实测记录" size:13 weight:UIFontWeightMedium color:UIColor.secondaryLabelColor];
    [memory addArrangedSubview:self.memoryStatus];
    self.memoryButton=[self button:@"运行权限验证" symbol:@"checkmark.shield" action:@selector(checkMemory) primary:NO];[memory addArrangedSubview:self.memoryButton];
    UIStackView *about=[self card:@"版本与适配" subtitle:nil];about.spacing=10;
    [about addArrangedSubview:[self label:[NSString stringWithFormat:@"NSP Pro %@\nNEW STATE 0.9.98（804）\n目标范围 <3000 米 · 相机三维距离\niPhone 15 Pro Max · iOS 17.0 · TrollStore",NSPVersion]
        size:14 weight:UIFontWeightMedium color:UIColor.labelColor]];
    [about addArrangedSubview:[self label:@"普通个人证书签名无法提供本工具所需权限。\n自瞄、内存修改与检测屏蔽尚未实现。"
        size:12 weight:UIFontWeightRegular color:UIColor.secondaryLabelColor]];
    [about addArrangedSubview:[self button:@"查看发布记录" symbol:@"arrow.up.right.square" action:@selector(openReleases) primary:NO]];
    [self.content addArrangedSubview:[self label:@"HUD 启动与窗口托管参考 Lessica/TrollSpeed（MIT）。许可随安装包提供。"
        size:11 weight:UIFontWeightRegular color:UIColor.tertiaryLabelColor]];
}
- (void)save {NSPWriteJSON(@"settings.json",self.settings);}
- (void)toggle:(UISwitch *)sender {
    self.settings[sender.accessibilityIdentifier]=@(sender.on);self.presets.selectedSegmentIndex=UISegmentedControlNoSegment;[self save];[self refresh];
}
- (void)orientationChanged:(UISegmentedControl *)sender {self.settings[@"orientation"]=@(sender.selectedSegmentIndex);[self save];}
- (void)updateSliderValue:(NSString *)key {
    float value=[self.settings[key] floatValue];
    self.values[key].text=[key isEqualToString:@"overlay_opacity"] ? [NSString stringWithFormat:@"%.0f%%",value*100] :
        [key isEqualToString:@"line_width"] ? [NSString stringWithFormat:@"%.1f pt",value] : [NSString stringWithFormat:@"%.1f×",value];
}
- (void)sliderChanged:(UISlider *)sender {
    self.settings[sender.accessibilityIdentifier]=@(sender.value);[self updateSliderValue:sender.accessibilityIdentifier];[self save];
}
- (void)presetChanged:(UISegmentedControl *)sender {
    NSInteger preset=sender.selectedSegmentIndex;
    if (preset<0) return;
    [self.settings addEntriesFromDictionary:@{@"characters":@YES,@"vehicles":@(preset>0),@"items":@NO,@"airdrops":@(preset>0),
        @"names":@(preset>0),@"distances":@YES,@"health":@(preset==2),@"health_bars":@(preset>0),
        @"boxes":@YES,@"lines":@(preset==2),@"edge_indicators":@(preset==2),@"crosshair":@YES,@"skeletons":@NO,@"hud_status":@NO,@"hide_teammates":@YES}];
    for (NSString *key in self.toggles) [self.toggles[key] setOn:[self.settings[key] boolValue] animated:YES];
    [self save];UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,@"显示预设已应用");
}
- (void)refresh {
    NSDictionary *report=NSPReadJSON(@"status.json"),*launch=NSPReadJSON(@"launch.json");
    NSTimeInterval now=NSDate.date.timeIntervalSince1970,stamp=[report[@"time"] doubleValue];
    BOOL fresh=stamp>0 && now>=stamp && now-stamp<7;
    BOOL matching=!launch || [launch[@"helper_pid"] intValue]==[report[@"helper_pid"] intValue];
    BOOL running=fresh && matching && [report[@"hosting_call_returned"] boolValue] && ![report[@"stopped"] boolValue] &&
        ![launch[@"phase"] isEqualToString:@"child_exited"];
    BOOL requested=[launch[@"phase"] isEqualToString:@"requested"] || [launch[@"phase"] isEqualToString:@"child_created"];
    BOOL waiting=self.startingHUD || (!running && requested && now-[launch[@"time"] doubleValue]<8);
    BOOL reading=[report[@"sampling"] boolValue] && [report[@"read_port_acquired"] boolValue] && [report[@"camera_ready"] boolValue];
    NSString *title=running ? ([report[@"sampling"] boolValue] ? (reading ? @"读取运行中" : @"等待游戏数据") : @"悬浮层已响应") : waiting ? @"正在启动…" : @"悬浮层未运行";
    self.status.text=title;self.statusDot.backgroundColor=running ? NSPAccent() : waiting ? UIColor.systemOrangeColor : UIColor.secondaryLabelColor;
    if (running) {
        self.statusDetail.text=report[@"status"]?:@"";
        NSString *version=report[@"version"];
        if (version && ![version isEqualToString:NSPVersion]) self.statusDetail.text=[self.statusDetail.text stringByAppendingFormat:@"\n悬浮层仍为 %@，请重新启动以使用新版。",version];
    } else if (waiting) self.statusDetail.text=@"正在等待悬浮层响应；进程创建不代表窗口已经显示。";
    else if (launch && [launch[@"error_code"] intValue]) self.statusDetail.text=launch[@"status"];
    else if (report && ![report[@"stopped"] boolValue]) self.statusDetail.text=@"尚未收到有效的近期响应，可启动悬浮层或在工具页查看诊断。";
    else self.statusDetail.text=@"点击启动后，返回游戏使用所选显示功能。";
    NSDictionary *counts=report[@"marker_counts"],*performance=report[@"performance"];
    if (running && [report[@"sampling"] boolValue]) {
        self.metrics.text=[NSString stringWithFormat:@"真人 %@ · 人机 %@ · 未知 %@\n载具 %@ · 空投 %@\n隐藏队友 %@ · 移除死亡 %@\n最近采样 %.1f ms / 调度目标 60 Hz",
            counts[@"humans"]?:@0,counts[@"bots"]?:@0,counts[@"unknown"]?:@0,counts[@"vehicles"]?:@0,counts[@"airdrops"]?:@0,
            report[@"affiliation"][@"teammates_hidden"]?:@0,report[@"affiliation"][@"dead_hidden"]?:@0,[performance[@"read_ms"] doubleValue]];
    } else self.metrics.text=@"";
    self.startButton.enabled=!self.startingHUD;
    UIButtonConfiguration *configuration=[self.startButton.configuration copy];
    configuration.title=self.startingHUD ? @"正在启动…" : running ? @"重新启动悬浮层" : @"启动悬浮层";
    configuration.showsActivityIndicator=self.startingHUD;self.startButton.configuration=configuration;
    self.memoryButton.enabled=!self.checkingMemory;
    NSDictionary *memory=NSPReadJSON(@"memory-check.json"),*memoryLaunch=NSPReadJSON(@"memory-check-launch.json");
    NSDictionary *control=memory[@"game_control"];
    if (self.checkingMemory) self.memoryStatus.text=@"验证运行中…";
    else if (control && [control[@"port_acquired"] boolValue]) self.memoryStatus.text=@"已取得游戏控制端口；没有测试游戏写入。";
    else if (control[@"task_for_pid_return"]) self.memoryStatus.text=[NSString stringWithFormat:@"未取得游戏控制端口（返回 %@）；未尝试游戏写入。",control[@"task_for_pid_return"]];
    else if (memoryLaunch) self.memoryStatus.text=memoryLaunch[@"status"]?:@"暂无完整结果，请导出诊断。";
}
- (void)showDiagnostics {
    UIViewController *details=[UIViewController new];details.title=@"诊断详情";details.view.backgroundColor=NSPBackground();
    UITextView *text=[[UITextView alloc] initWithFrame:CGRectZero];text.translatesAutoresizingMaskIntoConstraints=NO;
    text.editable=NO;text.alwaysBounceVertical=YES;text.backgroundColor=NSPBackground();text.textColor=UIColor.labelColor;
    text.font=[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];text.textContainerInset=UIEdgeInsetsMake(16,16,24,16);
    NSMutableString *summary=[NSMutableString stringWithFormat:@"NSP Pro %@\n%@\n\n",NSPVersion,NSProcessInfo.processInfo.operatingSystemVersionString];
    for (NSString *file in @[@"status.json",@"launch.json",@"memory-check.json",@"memory-check-launch.json"]) {
        NSDictionary *record=NSPReadJSON(file);if (!record) continue;
        NSData *data=[NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
        [summary appendFormat:@"%@\n%@\n\n",file,[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]];
    }
    text.text=summary;[details.view addSubview:text];
    [NSLayoutConstraint activateConstraints:@[[text.topAnchor constraintEqualToAnchor:details.view.safeAreaLayoutGuide.topAnchor],
        [text.bottomAnchor constraintEqualToAnchor:details.view.bottomAnchor],[text.leadingAnchor constraintEqualToAnchor:details.view.leadingAnchor],
        [text.trailingAnchor constraintEqualToAnchor:details.view.trailingAnchor]]];
    [self.navigationController pushViewController:details animated:YES];
}
- (void)openReleases {
    [UIApplication.sharedApplication openURL:[NSURL URLWithString:@"https://github.com/zwthys-cyber/newstate-ios-port/releases"] options:@{} completionHandler:nil];
}
#ifdef NSP_UI_PREVIEW
- (void)runPreviewChecks {
    NSMutableArray *checks=[NSMutableArray array],*failures=[NSMutableArray array];
    void (^check)(BOOL,NSString *)=^(BOOL passed,NSString *name) {
        [checks addObject:name];if (!passed) [failures addObject:name];
    };
    NSDictionary *original=[self.settings copy];
    nsport::Marker marker;marker.relation=nsport::Relation::enemy;marker.playerKind=nsport::PlayerKind::human;
    check([NSPCharacterColor(marker) isEqual:UIColor.systemRedColor],@"human enemy labels and rays use red");
    marker.playerKind=nsport::PlayerKind::bot;
    check([NSPCharacterColor(marker) isEqual:UIColor.systemYellowColor],@"bot labels and rays use yellow");
    marker.relation=nsport::Relation::unknown;
    check([NSPCharacterColor(marker) isEqual:UIColor.systemOrangeColor],@"unknown affiliation is not colored as a confirmed enemy");
    check([self.settings[@"hide_teammates"] boolValue],@"upgrade defaults enable teammate filtering");
    NSPWriteJSON(@"settings.json",@{@"sampling":@YES,@"boxes":NSNull.null,@"orientation":@99,@"label_scale":@9,@"overlay_opacity":@0,@"line_width":@"invalid"});
    NSDictionary *normalized=NSPSettings();
    check([normalized[@"sampling"] boolValue] && [normalized[@"boxes"] boolValue] &&
        [normalized[@"orientation"] intValue]==2 && [normalized[@"label_scale"] doubleValue]==1.3 &&
        [normalized[@"overlay_opacity"] doubleValue]==0.35 && [normalized[@"line_width"] doubleValue]==1.2,
        @"settings migration, invalid types and style bounds");
    check([normalized[@"airdrops"] boolValue] && ![normalized[@"items"] boolValue],@"upgrade enables airdrops independently of ground loot");
    self.settings=[original mutableCopy];self.settings[@"sampling"]=@YES;self.settings[@"orientation"]=@1;
    self.pages.selectedSegmentIndex=1;[self renderPage];
    for (NSInteger i=0;i<3;++i) {
        self.presets.selectedSegmentIndex=i;[self presetChanged:self.presets];
        check([self.settings[@"sampling"] boolValue] && [self.settings[@"orientation"] intValue]==1 &&
            ![self.settings[@"skeletons"] boolValue] && ![self.settings[@"hud_status"] boolValue],
            [NSString stringWithFormat:@"preset %ld preserves reading and orientation, keeps bones/status off",(long)i]);
    }
    UISwitch *dropToggle=self.toggles[@"airdrops"];dropToggle.on=NO;[self toggle:dropToggle];
    check(![NSPSettings()[@"airdrops"] boolValue] && ![NSPSettings()[@"items"] boolValue],@"airdrop toggle persists independently of ground loot");
    UISwitch *toggle=self.toggles[@"names"];toggle.on=NO;[self toggle:toggle];
    check(![NSPSettings()[@"names"] boolValue] && self.presets.selectedSegmentIndex==UISegmentedControlNoSegment,@"manual switch persists and clears preset selection");
    UISlider *slider=self.sliders[@"overlay_opacity"];slider.value=0.5;[self sliderChanged:slider];
    check([self.values[@"overlay_opacity"].text isEqualToString:@"50%"] && [NSPSettings()[@"overlay_opacity"] doubleValue]==0.5,@"style slider updates label and persistent value");
    self.pages.selectedSegmentIndex=0;[self renderPage];
    NSPLaunchReport(@"child_created",@"test",0,123,nil);[self refresh];
    check([self.status.text isEqualToString:@"正在启动…"],@"process creation alone is not reported as HUD response");
    NSPWriteJSON(@"status.json",@{@"time":@(NSDate.date.timeIntervalSince1970),@"helper_pid":@123,@"hosting_call_returned":@YES,@"sampling":@YES});[self refresh];
    check([self.status.text isEqualToString:@"等待游戏数据"],@"sampling request alone is not reported as live game reading");
    NSPWriteJSON(@"status.json",@{@"time":@(NSDate.date.timeIntervalSince1970-20),@"helper_pid":@123,@"hosting_call_returned":@YES});
    NSPLaunchReport(@"child_created",@"test",0,123,nil);[self refresh];
    check(![self.status.text isEqualToString:@"悬浮层已响应"],@"stale heartbeat cannot report an active HUD");
    self.pages.selectedSegmentIndex=2;[self renderPage];
    NSPWriteJSON(@"memory-check.json",@{@"stage":@"completed",@"game_control":@{@"port_acquired":@NO,@"task_for_pid_return":@5}});
    NSPWriteJSON(@"memory-check-launch.json",@{@"exit_code":@0});[self refresh];
    check([self.memoryStatus.text containsString:@"未取得游戏控制端口"],@"probe exit zero cannot report control-port success");
    for (NSInteger i=0;i<3;++i) {
        self.pages.selectedSegmentIndex=i;[self renderPage];[self.view layoutIfNeeded];
        check(self.content.bounds.size.width>0 && self.content.bounds.size.width<=self.scroll.bounds.size.width &&
            self.pages.frame.size.width>0 && self.content.frame.size.height>0 &&
            self.scroll.contentSize.width<=self.scroll.bounds.size.width+1,
            [NSString stringWithFormat:@"page %ld has valid vertical scroll layout",(long)i]);
    }
    self.settings=[original mutableCopy];[self save];
    for (NSString *name in @[@"status.json",@"launch.json",@"memory-check.json",@"memory-check-launch.json"])
        [NSFileManager.defaultManager removeItemAtPath:NSPPath(name) error:nil];
    self.pages.selectedSegmentIndex=0;[self renderPage];
    [self startHUD];[self stopHUD];
    check(!self.startingHUD && [NSPReadJSON(@"launch.json")[@"phase"] isEqualToString:@"stop_requested"],@"stop cancels a pending start before any helper spawn");
    [NSFileManager.defaultManager removeItemAtPath:NSPPath(@"launch.json") error:nil];[self refresh];
    [NSFileManager.defaultManager removeItemAtPath:NSPPath(@"status.json") error:nil];[self refresh];
    NSPWriteJSON(@"ui-smoke.json",@{@"passed":@(failures.count==0),@"checks":checks,@"failures":failures,
        @"scope":@"Simulator controller UI only; no game or device permissions tested"});
}
#endif
@end
