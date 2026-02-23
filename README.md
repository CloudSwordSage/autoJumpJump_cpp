# auto-jump-jump-cpp

🎮 基于 C++ 实现的微信"跳一跳"游戏自动辅助工具，使用计算机视觉和深度学习技术实现自动跳跃功能。

## 项目简介

本项目是一个高性能的实时游戏辅助系统，通过屏幕捕获、图像处理和目标检测技术，自动识别游戏场景中的平台位置，并计算合适的跳跃力度，实现自动化游戏。

## 核心特性

- 🚀 **高性能 C++ 实现** - 基于 C++17 标准，编译为本地代码，运行效率高
- 🎯 **YOLO 目标检测** - 集成 YOLO11n 模型，精准识别游戏平台
- 📸 **实时屏幕捕获** - 捕获游戏窗口画面，实时分析
- 🎨 **LAB 颜色空间分析** - 使用 LAB 颜色距离算法进行图像分割
- ⌨️ **自动输入控制** - 模拟鼠标/键盘操作，自动执行跳跃
- 📊 **性能统计监控** - 支持性能统计模式和实时监控窗口
- 🔧 **灵活配置** - JSON 配置文件，可调节各项参数

## 技术栈

| 组件      | 技术          |
| --------- | ------------- |
| 编程语言  | C++17         |
| 构建系统  | CMake 3.20+   |
| 深度学习  | ONNX Runtime  |
| 图像处理  | OpenCV 4.13.0 |
| JSON 解析 | nlohmann/json |
| 目标检测  | YOLO11n       |
| 平台      | Windows       |

## 目录结构

```
autoJumpJump/
├── include/              # 头文件
│   └── play_runner/      # 核心模块头文件
├── src/                  # 源代码
│   ├── play_runner/      # 核心模块实现
│   └── main.cpp          # 程序入口
├── config/               # 配置文件
│   └── play_runner.json  # 运行时配置
├── models/               # 模型文件
│   └── yolo11n_last.onnx # YOLO 检测模型
├── scripts/              # 脚本工具
├── build/                # 构建输出目录
├── dist/                 # 发布目录
├── logs/                 # 日志目录
└── third_party/          # 第三方依赖
    ├── onnxruntime/      # ONNX Runtime
    ├── opencv/           # OpenCV
    └── nlohmann/         # JSON 库
```

## 构建指南

### 前置要求

- Windows 10/11
- **MSYS2** (推荐) - 提供 MinGW-w64 工具链和 Unix 工具
- CMake 3.20 或更高版本
- Git

### 克隆项目

```bash
git clone https://github.com/CloudSwordSage/autoJumpJump_cpp.git
cd autoJumpJump_cpp
```

### 准备依赖 (使用 MSYS2)

`scripts/` 目录提供了依赖获取脚本，可在 MSYS2 环境中自动下载和准备所需依赖：

```bash
# 在 MSYS2 UCRT64 环境中运行
cd scripts
./get_third.sh    # 下载并准备第三方依赖
```

或者手动准备 `third_party/` 目录，确保包含以下依赖：

- ONNX Runtime (包含头文件、DLL 和库文件)
- OpenCV 4.13.0 (MinGW 静态库)
- nlohmann/json 头文件

### 编译项目 (使用 MSYS2)

推荐使用 MSYS2 UCRT64 环境进行编译：

```bash
# 在 MSYS2 UCRT64 环境中
mkdir build
cd build

# 配置 CMake
cmake .. -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
# 构建项目
cmake --build . --config Release
```

构建完成后，可执行文件和依赖将自动复制到 `dist/` 目录。

## 使用方法

### 运行程序

```bash
cd dist
./cpp-play-runner.exe
```

### 配置说明

编辑 `config/play_runner.json` 文件以自定义行为：

```json
{
  "process_name": "跳一跳",              // 目标游戏进程名
  "debug": false,                        // 性能统计模式
  "window_title": "Real-time Monitor",   // 监控窗口标题

  // 屏幕裁剪区域
  "crop_top": 10,
  "crop_bottom": 10,
  "crop_left": 10,
  "crop_right": 10,

  // ROI 区域设置
  "roi_top_margin": 250,
  "roi_bottom_margin": 100,

  // LAB 颜色参数
  "lab_l": 63,
  "lab_a": 141,
  "lab_b": 109,
  "lab_distance_threshold": 15,

  // 跳跃力度参数
  "jump_alpha": 2.185,
  "jump_beta": 0.0,

  // YOLO 模型配置
  "onnx_model_path": "models/yolo11n_last.onnx",
  "onnx_input_width": 640,
  "onnx_input_height": 640,
  "onnx_score_threshold": 0.25,
  "onnx_nms_iou_threshold": 0.45,

  // 监控窗口
  "enable_monitor_window": true,

  // 日志配置
  "log_level": "info",
  "log_file_path": "logs/cpp_play_runner.log"
}
```

### 主要参数说明

| 参数                    | 说明                               |
| ----------------------- | ---------------------------------- |
| `process_name`          | 目标游戏窗口/进程名称              |
| `debug`                 | 开启性能统计模式                   |
| `enable_monitor_window` | 是否启用实时监控窗口（遮盖层显示） |
| `jump_alpha`            | 跳跃力度系数，影响跳跃距离计算     |
| `lab_*`                 | LAB 颜色空间参数，用于图像分割     |
| `onnx_*`                | YOLO 模型推理参数                  |

## 工作原理

1. **屏幕捕获** - 实时捕获游戏窗口画面
2. **图像预处理** - 裁剪、缩放、颜色空间转换
3. **目标检测** - 使用 YOLO11n 模型检测平台位置
4. **颜色分析** - LAB 颜色距离算法辅助定位
5. **力度计算** - 根据平台距离计算跳跃力度
6. **输入控制** - 模拟按压操作执行跳跃

## 许可证

本项目采用 [Apache License 2.0](LICENSE) 许可证。

## 注意事项

- 本工具仅供学习和技术研究使用

## 致谢

- [ONNX Runtime](https://onnxruntime.ai/) - 高性能推理引擎
- [OpenCV](https://opencv.org/) - 开源计算机视觉库
- [YOLO](https://github.com/ultralytics/ultralytics) - 实时目标检测算法
- [nlohmann/json](https://github.com/nlohmann/json) - C++ JSON 库
