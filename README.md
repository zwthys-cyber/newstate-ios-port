# NEW STATE iOS Port

NEW STATE **0.9.98（804）** 的实验性只读显示工具。当前交付为 **TrollStore 独立 IPA**，目标设备 iPhone 15 Pro Max / **iOS 17.0**，开发者模式保持关闭。游戏 IPA 保持原样，不向游戏注入。

用户已在 0.2.4 确认悬浮层、只读采样、人物姓名/距离/方框、载具标记/距离可见；其他功能与准确性仍需逐项验证。

0.2.5 针对移动延迟增加位置采样频率、减少单轮对象发现预算并缓存元数据，记录实际采样耗时/间隔；新增血量条和屏外方向提示。30Hz 为目标，不是设备实测结果。尚未实现队伍区分、自瞄、后坐力修改或检测屏蔽。

- [独立 IPA 安装、功能和验证说明](apps/ExternalPort/README.zh-CN.md)
- [历史动态库排查记录](README.zh-CN.md)
- [静态分析依据](docs/STATIC-ANALYSIS.zh-CN.md)
- [GitHub Actions 构建](../../actions/workflows/build.yml)

源码通过 GitHub 托管 macOS / Xcode 26.6 编译。公开仓库不包含游戏主程序、APK、IPA、框架或设备日志。

另提供独立的[开发签名安装测试 IPA](apps/DeveloperModeCheck/README.zh-CN.md)，用于全能签开发签名安装流程；应用自身无法开启开发者模式。
