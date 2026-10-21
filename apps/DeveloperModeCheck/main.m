#import <UIKit/UIKit.h>

@interface DMCSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end

@implementation DMCSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *controller=[UIViewController new];
    controller.title=@"开发签名测试";
    controller.view.backgroundColor=UIColor.systemBackgroundColor;
    UITextView *text=[UITextView new];
    text.editable=NO;
    text.font=[UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    text.adjustsFontForContentSizeCategory=YES;
    text.textColor=UIColor.labelColor;
    text.backgroundColor=UIColor.systemBackgroundColor;
    text.text=@"这个测试应用已能启动。\n\n"
        @"应用本身不会打开或修改开发者模式。安装开发签名应用可能使系统设置中的开发者模式入口显示；运行结果不能证明当前使用了开发签名。\n\n"
        @"请到：设置 → 隐私与安全性 → 开发者模式。\n\n"
        @"若入口出现，由你手动开启，按系统提示重启，再解锁确认。\n\n"
        @"如果入口仍未出现，请检查全能签使用的是 Apple Development／iPhone Developer 证书及匹配的开发描述文件。Distribution、企业签名或巨魔安装不能作为本次开发签名触发测试的替代。\n\n"
        @"此包不读取游戏、不修复 TrollStore、不修改系统文件、不收集数据。版本 0.1.0。";
    text.translatesAutoresizingMaskIntoConstraints=NO;
    [controller.view addSubview:text];
    UILayoutGuide *safe=controller.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [text.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:16],
        [text.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-16],
        [text.topAnchor constraintEqualToAnchor:safe.topAnchor constant:12],
        [text.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-12]]];
    self.window.rootViewController=[[UINavigationController alloc] initWithRootViewController:controller];
    [self.window makeKeyAndVisible];
}
@end

@interface DMCAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation DMCAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application; (void)options;
    return YES;
}
@end

int main(int argc,char *argv[]) {
    @autoreleasepool {return UIApplicationMain(argc,argv,nil,NSStringFromClass(DMCAppDelegate.class));}
}
