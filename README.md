# auto-jump-jump-cpp

基于 C++17 实现的微信「跳一跳」自动辅助工具。核心目标就两点：跑得快、跳得准。整体用 **实时屏幕捕获 + OpenCV 预处理 + YOLO(ONNX Runtime) 平台检测 + LAB 角色定位 + 几何对齐 + 自适应力度拟合** 完成闭环。

本项目强调工程化可维护：**配置驱动**（`config/play_runner.json`）、**可视化 UI**（ImGui）、**性能统计**（耗时/帧率/线程状态）、以及用于校准与定位偏差的调试能力。

## 功能概览

- C++ 原生高性能实现：C++17 + CMake，适合做实时流水线与多线程
- YOLO11n(ONNX) 平台检测：检测候选落点/目标方块，减少传统视觉规则的脆弱性
- OpenCV 预处理：ROI 裁剪、缩放、颜色空间转换，尽量把「算力」花在关键区域
- LAB 空间角色定位：用于角色区域/脚底点定位（降低颜色与光照变化影响）
- 自动输入控制：模拟按压时长，实现跳跃执行
- 自适应跳跃力度：在线采样 + **DP 分段线性回归**（BIC 选择分段数），自动更新参数
- 调试/校准能力：全局热键输出坐标快照，支持脚底中心偏移校准

## 技术栈

| 组件         | 技术          |
| ------------ | ------------- |
| 编程语言     | C++17         |
| 构建系统     | CMake 3.20+   |
| 深度学习推理 | ONNX Runtime  |
| 图像处理     | OpenCV 4.13.0 |
| 数值计算     | Eigen         |
| JSON         | nlohmann/json |
| UI           | ImGui         |
| 平台         | Windows       |

## 目录结构

```
autoJumpJump/
├── include/               # 头文件
│   └── play_runner/       # 核心模块头文件
├── src/                   # 源代码
│   ├── play_runner/       # 核心模块实现
│   └── main.cpp           # 程序入口
├── config/                # 配置文件
│   └── play_runner.json   # 运行时配置（检测/几何/跳跃/自适应）
├── models/                # 模型文件
│   └── yolo11n_last.onnx  # YOLO 检测模型（ONNX）
├── scripts/               # 脚本工具（依赖获取、打包等）
├── build/                 # 构建输出目录
├── dist/                  # 发布目录（exe + dll）
├── logs/                  # 日志目录
├── imgui/                 # ImGui 库目录
└── third_party/           # 第三方依赖
    ├── eigen/
    ├── onnxruntime/
    ├── opencv/
    └── nlohmann/
```

## 快速开始

### 前置要求

- Windows 10/11
- MSYS2（UCRT64）或同等 MinGW-w64 工具链
- CMake 3.20+
- Git

### 克隆项目

```bash
git clone https://github.com/CloudSwordSage/autoJumpJump_cpp.git
cd autoJumpJump_cpp
```

### 准备依赖（MSYS2）

`scripts/` 目录提供依赖获取脚本，可在 MSYS2 环境中下载并准备依赖：

```bash
cd scripts
./get_third.sh
```

也可以手动准备 `third_party/`，至少需要：

- Eigen 头文件
- ONNX Runtime（头文件 + DLL + lib）
- OpenCV 4.13.0（MinGW 静态库或等价形式）
- nlohmann/json 头文件

### 构建（MSYS2 UCRT64）

```bash
mkdir build
cd build
cmake .. -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

构建完成后，产物与依赖会被复制到 `dist/`。

### 运行

```bash
cd dist
./cpp-play-runner.exe
```

### 热键操作

- `Ctrl+S`：开始自动跳跃
- `Ctrl+D`：停止自动跳跃
- `SPACE`：单次跳跃
- `Ctrl+Q`：导出历史样本(logs目录)

## 工作流程（高层）

1. 屏幕捕获：采集游戏画面帧
2. 预处理：裁剪 ROI、缩放、颜色空间转换
3. 目标检测：YOLO 推理，得到平台候选框
4. LAB 空间角色定位：用于角色区域/脚底点定位（降低颜色与光照变化影响）
5. 几何定位：从候选框中选目标平台中心，结合角色脚底点做方向与距离
6. 力度计算：由「距离」映射为「按压时长」
7. 输入执行：模拟按压完成跳跃
8. 在线学习：落地稳定后记录样本，更新自适应参数（可选）

## 关键算法与实现要点

### 1) 距离建模：用“脚底中心”对齐几何中心

跳一跳的关键不是“看见目标”，而是“你的距离定义要稳定”。本项目把几何计算统一到一个坐标系里：

- `raw_foot`：检测得到的角色脚底点（原始）
- `corrected_foot = raw_foot - (foot_center_offset_x, foot_center_offset_y)`：用于几何计算的校准脚底中心

强约束：

- **所有几何计算**（目标选择、目标距离、落地偏移、方向向量）都使用 `corrected_foot`
- **稳定性检测**必须使用 `raw_foot`（避免“校准反馈环路”导致抖动误判）

这就是所谓“对齐几何中心”：你用来算距离的点必须长期一致，否则你拟合出来的力度参数就是在给噪声背锅。

### 2) 在线自适应力度：DP 分段线性回归（BIC 选段）

传统做法往往用一个线性模型 `press_ms = alpha * distance + beta` 直接硬套，但跳一跳内部更像是“力度向量 + 正交变换到屏幕”的组合过程，距离到按压时长的映射不必然是一条一次函数，更可能接近某个高次但整体平滑的曲线。因此这里采用在线学习：用分段拟合去近似这条平滑曲线，并在运行中持续修正。

- 每次跳跃完成后记录样本 `(x, y)`：
  - `y`：实际按压时长（ms）
  - `x`：实际距离（不是简单的欧氏距离），使用
    - `x = a + b * sign`
    - `a`：目标距离（基于 `corrected_foot`）
    - `b`：落地偏移（基于 `corrected_foot`）
    - `sign`：与方向有关的符号项
- 样本记录必须满足：**按键结束**且**角色位移停止并稳定落地**（否则样本污染模型）
- 跳跃失败时默认丢弃最近的若干样本（避免异常点把模型带歪）

拟合策略（分段近似平滑曲线）：

- 用动态规划（DP）在 `x` 轴上做 **分段线性回归**，在多个候选分段数中选择最优
- 用 BIC（Bayesian Information Criterion）在“拟合误差 vs 复杂度”之间取平衡，防止过拟合
- 最终得到若干段 `(x_begin, x_end, alpha, beta)` 写回配置
- 约定：`x_end = -1` 表示该段覆盖无穷大

回退机制：

- 若自适应分段为空或当前距离不在分段覆盖范围内，则回退使用全局 `jump_alpha`、`jump_beta`

这套东西的目的很简单：让你的“力度曲线”能随环境改变自动修正，又不至于像无脑高阶拟合一样抖成筛子。

### 3) LAB 空间角色定位：用于脚底点提取

LAB 空间在这里主要用于**角色定位**（尤其是脚底点提取与稳定性判断的输入），而不是用来做平台定位主逻辑。相较 RGB，LAB 对光照与色偏更“接近感知距离”，适合做简单的颜色阈值/距离分割来得到更稳定的角色区域，从而让后续几何计算和采样更可靠。

### 4) 采样触发与稳定性判定：避免过早记录

在线学习最常见的坑：角色还在弹跳/回弹/缓动，你就把脚底点当落地点记了。结果就是模型学到了一堆错误距离。

因此采样触发严格依赖：

- 输入线程确认按压已释放
- 位移趋于稳定（基于 `raw_foot` 的稳定性判定）
- 满足稳定后才记录 `(x, y)`

## 性能与工程优化要点

- ROI 裁剪与缩放：把检测/预处理约束在关键区域，降低推理与像素操作量
- 多线程流水线：输入、捕获、推理、UI、统计等解耦，避免互相阻塞
- 内存复用：Mat/缓冲区复用，减少频繁分配释放带来的抖动
- 统计与可观测性：关键路径耗时与自适应拟合结果用 info 级别日志输出，便于定位性能/精度退化
- 配置驱动：所有核心阈值、偏移、模型参数都从 JSON 加载，支持快速迭代调参

## 调试与校准

为了处理“脚底检测点 != 几何中心”的系统偏差，项目提供了校准机制与调试输出：

- `foot_center_offset_x / foot_center_offset_y`：把检测到的脚底点校准到计算用的脚底中心

## 许可证

本项目采用 [Apache License 2.0](LICENSE) 许可证。

## 注意事项

- 本工具仅供学习与技术研究使用
- 游戏画面、分辨率、窗口缩放、输入设备都会显著影响参数，需要配合配置与校准使用

## 致谢

- [Eigen](https://eigen.tuxfamily.org/index.php?title=Main_Page)
- [Imgui](https://imgui.org/)
- [OpenCV](https://opencv.org/)
- [YOLO](https://github.com/ultralytics/ultralytics)
- [nlohmann/json](https://github.com/nlohmann/json)
