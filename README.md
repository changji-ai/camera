# camera — 摄像头实时识别（本地 AI）

Qt 6 + C++ 桌面工具：实时预览摄像头画面，用**本地** ONNX 模型（YOLO11n · ONNX Runtime CPU）逐帧识别画面中的物体，直接在视频上叠加检测框和类别，全程无需联网、不上传任何画面。

![platforms](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-blue)
![license](https://img.shields.io/badge/license-MIT-green)

## 功能

- 实时摄像头预览（Qt Multimedia，自动枚举/热插拔摄像头设备）
- 本地推理：YOLO11n (COCO 80 类) + ONNX Runtime，CPU 即可实时运行
- 叠加显示检测框、类别、置信度；置信度阈值可调
- 实时统计：推理耗时、推理 FPS、预览 FPS、目标摘要
- 推理在独立线程，推理不过来时自动丢帧，预览始终流畅
- 可加载任意 ultralytics 导出的 detection ONNX 模型

## 下载

从 [Releases](https://github.com/changji-ai/camera/releases/latest) 获取最新版本（每次推送到 main 自动构建发布）：

| 平台 | 文件 |
| --- | --- |
| Windows x64 | `camera-*-windows-x64.zip` |
| Windows arm64 | `camera-*-windows-arm64.zip` |
| macOS Apple Silicon | `camera-*-macos-arm64.zip` |
| macOS Intel | `camera-*-macos-x64.zip` |
| Linux x64 | `camera-*-linux-x64.tar.gz` |
| Linux arm64 | `camera-*-linux-arm64.tar.gz` |

各产物均自带 Qt 运行库、ONNX Runtime 与 `yolo11n.onnx` 模型，解压即用。

### 运行

- **Windows**：解压后运行 `camera.exe`。
- **macOS**：解压后如果提示无法验证，执行
  `xattr -dr com.apple.quarantine camera.app` 再打开。
- **Linux**：解压后运行 `./run.sh`（自带全部依赖库，无需安装 Qt）。

## 从源码构建

依赖：CMake ≥ 3.21、支持 C++17 的编译器、Qt ≥ 6.5（Widgets + Multimedia 模块）。
ONNX Runtime（1.20.1）会在配置阶段按平台自动下载，也可用环境变量 `ONNXRUNTIME_ROOT` 指向本地解压目录。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # Linux/macOS
cmake --build build --parallel
ctest --test-dir build                      # headless 冒烟测试（无需摄像头）
```

Windows 示例（Qt 官方 MSVC 套件 + Visual Studio）：

```bat
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --parallel
```

## 技术说明

- **推理**：ONNX Runtime CPU EP；输入 640×640，letterbox + 双线性缩放预处理、逐类 NMS 后处理均为手写实现，不依赖 OpenCV。
- **模型**：[YOLO11n](https://docs.ultralytics.com/models/yolo11/)（约 10MB），COCO 80 类，权重以 `models/yolo11n.onnx` 随仓库分发。更换自训练模型：导出 `onnx`（opset ≥ 12）后在界面「选择模型」加载即可。
- **线程模型**：UI 线程只做取帧与绘制；推理线程忙时新帧直接丢弃，避免排队延迟。
- **打包**：GitHub Actions 六平台矩阵（win x64/arm64、macOS x64/arm64、Linux x64/arm64），Windows 用 windeployqt、macOS 用 macdeployqt + ad-hoc 签名、Linux 递归收集动态库自包含打包。

## License

MIT
