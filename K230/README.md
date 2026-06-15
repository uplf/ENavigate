项目本部分代码请见：github@aluohappy

# K230 YOLO HTTP blob识别

基于 **嘉楠科技 K230 芯片** 的嵌入式 AI 视觉系统，集成 YOLO 目标检测、黑线循迹、串口通信、HTTPS 图片上传、视频录制等功能。

## 项目文件说明

| 文件 | 功能描述 |
|------|----------|
| `test9_try_fornew .py` | **主程序** — YOLO检测 + 黑线循迹 + HTTP上传 + 串口状态机，集成拍照工作流 |
| `test6_videorecord_indark.py` | **MP4录制模块** — 基于 K230 media 库的 H.265 硬编码视频录制（⚠️ 仅适用于固件版本 **1.3.5**） |
| `test1.py` | **离线抽帧工具** — 从视频文件中均匀抽取帧图片，用于数据集构建 |
| `mp_deployment_source/deploy_config.json` | **模型部署配置** — 推理参数、anchor、类别标签等 |
| `mp_deployment_source/best_20260516.kmodel` | **YOLO检测模型** — 训练好的 kmodel 文件（8类目标检测） |

## 功能特性

### 1. YOLO 目标检测
- 8类目标：`car`, `people`, `cat`, `dog`, `stop`, `motor`, `tree`, `slave`
- 基于状态机的目标追踪（命中/丢失计数）
- 活体抖动过滤，避免底层脏数据干扰
- 每3帧推理一次，平衡性能与实时性

### 2. 黑线循迹
- 基于 LAB 色彩空间的色块检测
- 实时偏移量计算用于 PID 控制
- 交叉节点自动识别与计数

### 3. 拍照工作流
- UART 串口触发（命令协议：`C1+N5+时间戳`）
- 自动连拍5张并逐张通过 HTTPS 上传至远端服务器
- 模式互斥：拍照时暂停 AI 推理，拍完后自动恢复

### 4. HTTP/HTTPS 上传
- SSL/TLS 加密传输
- Multipart 图片上传至指定 API
- 连接超时与端口复用（`SO_REUSEADDR`）

### 5. 视频录制
> ⚠️ **注意**：该功能仅适用于 K230 固件版本 **1.3.5**，其他固件版本可能存在兼容性问题。

- H.265 硬件编码 MP4 容器封装
- 定时阻塞录制模式
- 串口指令触发切换

## 硬件平台

- **芯片**: 嘉楠科技 K230 (RISC-V 双核 + KPU)
- **摄像头**: 板载 Sensor (320×240 / 640×480)
- **显示屏**: ST7701 LCD
- **通信**: UART 串口 (115200 bps) + WiFi

## 依赖库

- `nncase_runtime` — KPU 推理运行时
- `media.sensor` / `media.display` / `media.media` — K230 媒体子系统
- `ulab.numpy` — 轻量级数值计算
- `libs.PipeLine`, `libs.AIBase`, `libs.AI2D`, `libs.PlatTasks` — 官方 AI 管线工具
- `ybUtils.YbUart`, `libs.YbProtocol` — 串口通信

## 部署配置 (`deploy_config.json`)

```json
{
    "model_type": "AnchorBaseDet",
    "img_size": [320, 320],
    "confidence_threshold": 0.4,
    "nms_threshold": 0.5,
    "categories": ["car", "people", "cat", "dog", "stop", "motor", "tree", "slave"],
    "kmodel_path": "best_20260516.kmodel"
}
```

## 快速开始

1. 将 `mp_deployment_source/` 目录拷贝至 K230 开发板的 `/sdcard/mp_deployment_source/`
2. 修改 `test9_try_fornew .py` 中的 WiFi 账号密码和服务器地址
3. 上电运行主程序即可进入 YOLO + 循迹模式
4. 通过串口发送 `1` 进入拍照工作流

---

