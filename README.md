# Waveshare Desktop + Codex / DSH Apps

这个仓库现在以 **Waveshare ESP32-S3-Touch-LCD-1.85B Desktop** 为唯一固件基座：

- ESP-Brookesia Phone 负责桌面、状态栏、应用启动器、返回/最近任务和页面生命周期；
- 设置、音乐、相册是基座自带应用；
- Codex 控制台和 DSH 都以独立 Phone App / plugin 形式接入，Companion 现在是开机默认入口，原生桌面保留为返回后的后台入口；
- 小蓝使用已验收的 v2 动画帧，作为桌面层常驻宠物默认显示，可直接拖动位置；
- 原来的 Arduino `DialUi + AppShell` 已移出构建，不再作为系统壳层。

上游桌面参考：[STUPIDDDD0/waveshare-ESP32-S3-Touch-LCD-1.85B-desktop](https://github.com/STUPIDDDD0/waveshare-ESP32-S3-Touch-LCD-1.85B-desktop)。本仓库保留其 ESP-IDF/LVGL9 桌面运行时，并在 `firmware/main/app_codex/`、`firmware/main/app_dsh/` 增加两个独立应用插件。

## 统一入口

打开 [Codex Agent Companion 统一入口](https://yhyh0000.github.io/dsh-esp32-dial/)，首页是设备 UI 模拟器，顶部 Tab 可切换到在线烧录。连接标有 USB 的接口，选择对应串口后烧录。每次推送到 `main`，GitHub Actions 会使用 ESP-IDF 5.5.3 构建并发布合并镜像。

设备 UI 调试页保留兼容地址：[Codex Agent Companion](https://yhyh0000.github.io/dsh-esp32-dial/companion-ui-v1.html)。独立烧录页保留在 [flasher.html](https://yhyh0000.github.io/dsh-esp32-dial/flasher.html)。本地调试时运行 `python -m http.server 4173` 后打开 `http://127.0.0.1:4173/companion-ui-v1.html`。

## 构建结构

```text
firmware/
  components/                  ESP-Brookesia、Waveshare BSP 及依赖
  main/main.cpp                Desktop 唯一入口
  main/app_dsh/                DSH Phone App 插件
  main/app_codex/              Codex 全屏控制台 Phone App 插件
  main/app_xiaolan/            小蓝桌面常驻动画组件与帧资源
  main/app_wrappers/           Settings/Music/Gallery 应用
  main/dark/                   360x360 暗色桌面样式
bridge/                         电脑侧 Codex/DSH 桥接进程
docs/                           WebSerial 烧录页面和设计文档
firmware-legacy/                本地保留的旧 Arduino 实现，不参与构建
```

## 许可证

本项目新增代码采用 MIT。ESP-Brookesia 和 Waveshare 组件的原许可证随 `firmware/components` 保留。
