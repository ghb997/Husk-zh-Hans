# Husk 简体中文构建

本仓库是 [Leviidev/Husk](https://github.com/Leviidev/Husk) 的独立简体中文构建，基于上游正式标签 `3.0`、提交 `cffaf8b7b8eec773b4c69838e04c04b3d56b8139`，版本为 **3.0.0（129）**。此构建不是原作者发布的正式中文版。

Husk 用于在 iPhone / iPad 上通过转译层或完整 Android 模拟环境运行 Android 应用；3.0 新增 PS4 游戏支持、主机风格首页和首次设置。运行兼容性取决于上游实现、设备及 JIT 状态。

## 汉化范围

- 应用库、商店、发现、文件、下载、应用详情与设置。
- 首次启动引导、JIT 开启与配对说明、权限提示及错误消息。
- 存档、窗口、虚拟手柄、运行诊断和报告。
- 下载实时活动、灵动岛及 JIT 辅助扩展。
- 原生层的设备检查、兼容性扫描和错误说明。
- 常见问题、兼容性反馈、节日装饰设置及失败项目重新测试。
- 日期与时长采用中文格式，应用商店内容优先选择中文。
- 3.0 新首页、应用库筛选与状态、首次设置及导览、PS4 导入与运行提示、字体设置和更新说明。

翻译对照保存在 localization/zh-Hans.json 和 localization/native-zh-Hans.json 中。此版本直接使用中文界面，保留产品名、技术标识、包名、接口键及用于故障分析的原始日志；第三方应用内容由相应来源提供。用户已有的旧诊断记录不会被重新翻译。

应用更新提示使用本仓库的正式发布渠道，避免把中文版更新为上游英文版。Android 镜像继续使用上游维护的来源。

## 构建和下载

**3.0.0（129）** 已完成源码汉化，安装包正在 GitHub Actions 构建和校验。构建通过后会发布新的版本；此前的 2.0.0 安装包继续保留在 [历史发布](https://github.com/ghb997/Husk-zh-Hans/releases/tag/v2.0.0)。

- [发布说明、安装包、SHA-256 与验证报告](https://github.com/ghb997/Husk-zh-Hans/releases)
- [构建记录](https://github.com/ghb997/Husk-zh-Hans/actions/workflows/build-zh-hans.yml)
- [SideStore / AltStore 侧载源](https://raw.githubusercontent.com/ghb997/Husk-zh-Hans/main/altsource.json)

主应用最低 iOS 15.5；PS4 游戏需要 iOS 17.4，下载实时活动需要 iOS 16.1，内置 JIT 辅助扩展需要 iOS 26，其他系统使用受支持的 JIT 方式。首次设置中的语言、地区和时区偏好目前由上游保存，尚未接入 Android / PS4 运行时。

打开 [GitHub Actions](https://github.com/ghb997/Husk-zh-Hans/actions/workflows/build-zh-hans.yml)，选择 Run workflow，并保持 dependencies_only 为 false。成功后下载 Husk-3.0.0-zh-Hans 构建产物。

工作流在 macOS 上编译原生依赖及应用，从上游公开的 Lineage 镜像提取 Java / Flutter 运行资源，并校验镜像 SHA-256。保留中日韩字库，避免 Android 应用中的中文缺字。

PS4 是预编译依赖：上游的 `build_ps4_ios.sh` 指向作者本机 Bachata-S4 的 `husk` 分支及 husk-fex 的 `husk-fex` 分支；同步 3.0 时未找到这些对应的公开源码。`prepare_ps4_module.py` 从 [官方 3.0 IPA](https://github.com/Leviidev/Husk/releases/download/3.0/Husk.ipa) 提取 `libhuskps4.dylib`，固定校验整个 IPA 的 SHA-256 为 `112a1ddbb3143ae25e80c18c5b195b6dfa5067d66cbf99d2188fc03f4ab0f61d`，检查 arm64 架构、最低系统和依赖，并保存随包许可及 `ps4-module.provenance.json` 来源记录。模块在打包时重新临时签名。主应用、主机宿主接口、QEMU 和其他既有原生依赖仍从源码构建；本仓库不声称已从源码重建该 PS4 模块。模块内部的原始诊断文本保留，Husk 自身的界面和宿主错误提示已汉化。

IPA 采用上游的临时签名打包方式，常规侧载工具需要使用安装者的签名重新签署。TIPA 使用上游的 TrollStore 打包配置；是否能安装、启用 JIT 和运行应用，取决于设备及系统支持情况。

本地构建需要 macOS、Xcode 27.0（内置 StikJIT 框架需要 Swift 6.4）、Homebrew、Rust 和 Java 17。CI 使用 GitHub 的 xcode-27 构建环境，并将依赖缓存与旧版 Xcode 隔离。准备构建工具后：

    xcodebuild -downloadComponent MetalToolchain
    brew install meson ninja pkg-config xcodegen qemu autoconf automake libtool cmake bison flex e2fsprogs
    python3 -m venv .venv
    source .venv/bin/activate
    python -m pip install PyYAML==6.0.3 Mako==1.3.10 packaging==25.0
    rustup target add aarch64-apple-ios
    export JAVA_HOME=$(/usr/libexec/java_home -v 17)
    python3 scripts/check_localization.py
    python3 scripts/prepare_android_resources.py
    ./scripts/ci_build.sh "$PWD/build/Husk-3.0.0-zh-Hans.ipa"
    python3 scripts/verify_zh_ipa.py build/Husk-3.0.0-zh-Hans.ipa build/Husk-3.0.0-zh-Hans.tipa

CI 会在编译原生依赖前检查三个 iOS 目标的 Swift 语法与类型，随后验证翻译条目、格式占位符、包内中文元数据、必要运行资源和 ZIP 完整性，并生成带 SHA-256 的验证报告。静态校验不等于真机验证；本仓库未声明已完成 iPhone / iPad 安装、界面或游戏运行测试。

## 来源和许可

保留上游作者署名及 GPL-2.0-or-later 许可，详见 LICENSE、原始 README 和各组件说明。Android、字体及第三方运行库沿用各自许可证，构建脚本保留 APEX 模块附带的 NOTICE 文件。本仓库的源代码及构建脚本公开，方便复查和重新构建。
