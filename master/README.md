# AGV 主控系统 (Master)

多AGV导航系统的主机端程序，运行于 **i.MX6ULL (Linux + Debian)** 平台，负责路径规划、任务调度、从机通信和用户交互。

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    用户交互层                                 │
│       浏览器 (SVG 拓扑地图)  ←→  Nginx + FastCGI              │
├─────────────────────────────────────────────────────────────┤
│                    应用服务层                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐ │
│  │ planner  │  │  router  │  │  http_   │  │  mqtt_       │ │
│  │ 路径规划  │  │ 转向指引  │  │  service │  │  service     │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └──────┬───────┘ │
├───────┴──────────────┴─────────────┴─────────────────┴───────┤
│                    核心模型层                                 │
│             model_manager → 共享内存 (SHM)                     │
│    ┌─────────────────────────────────────────────┐           │
│    │  MapData (seqlock) │ CarData (seqlock)       │           │
│    └─────────────────────────────────────────────┘           │
├─────────────────────────────────────────────────────────────┤
│                    系统服务层                                 │
│   POSIX MQ  │  signalfd   │  timerfd   │  结构化日志          │
├─────────────────────────────────────────────────────────────┤
│                    硬件抽象层                                 │
│          网络接口 (eth0/wlan0)  │  系统时钟                    │
└─────────────────────────────────────────────────────────────┘
```

### 进程通信方式

| 方式 | 用途 | 说明 |
|------|------|------|
| **POSIX MQ** | 进程间任务调度、命令下发 | 三条队列：`mq_task_dispatch`、`mq_mqtt_publish`、`mq_route_exert` |
| **共享内存 (SHM)** | 地图数据、小车状态共享 | Seqlock + ProcMutex 实现无锁读/写者互斥 |
| **signalfd** | 信号处理 | 信号→fd，融入 poll 事件循环 |
| **timerfd** | 定时器 | 内核定时器 fd，精度高 |

---

## 目录结构

```
master/
├── CMakeLists.txt              # 顶层 CMake 构建文件
├── README.md                   # 本文件
├── devLog.md                   # 开发日志
├── system-api.md               # 系统 HTTP API 文档
├── system-api.pdf              # API 文档 PDF
├── mqtt-api.md                 # MQTT 通信协议文档
│
├── include/                    # 公共头文件
│   ├── config/
│   │   └── config.h           # 系统配置常量（节点数、小车数、MQTT 地址等）
│   └── agv/
│       ├── log_msg.h          # 日志消息结构（LogMsg、LogLevel）
│       └── mq_msg.h           # MQ 消息结构（TaskDispatchMsg、MqttPublishMsg、RouteExertMsg）
│
├── lib/                        # 基础设施库
│   ├── CMakeLists.txt
│   ├── ipc/                   # 进程间通信组件
│   │   ├── mq_wrapper.h       # POSIX MQ 封装（MqReceiver / MqSender / MqOwner）
│   │   ├── seqlock.h          # Seqlock 顺序锁 + ProcMutex 进程共享互斥锁
│   │   ├── shm_layout.h       # 共享内存布局定义（ShmHeader / MapData / CarData / ShmLayout）
│   │   ├── shm_manager.h      # SHM 生命周期管理（ShmOwner / ShmClient）及读写辅助
│   │   ├── signal_handler.h   # signalfd 统一信号处理框架
│   │   └── secure_exit.h      # 安全退出序列（注册清理动作、析构自动执行）
│   ├── logger/                # 日志系统
│   │   ├── CMakeLists.txt
│   │   ├── logger.h           # 日志接口（LOG_DEBUG/INFO/WARN/ERROR/FATAL）
│   │   └── logger.cpp         # 日志实现（通过 POSIX MQ 发往 log_daemon）
│   └── timer/                 # 定时器
│       ├── timer_fd.h
│       └── timer_fd.cpp
│
├── src/                        # 业务进程
│   ├── CMakeLists.txt
│   │
│   ├── model_manager/         # 进程 A — 模型管理器
│   │   ├── CMakeLists.txt
│   │   └── model_manager.cpp  # 初始化 SHM、创建 MQ、写入初始拓扑与小车数据
│   │
│   ├── planner/               # 路径规划进程
│   │   ├── planner.h          # 规划器类（A* 算法、任务处理）
│   │   └── planner.cpp        # 入口：poll loop 监听 mq_task_dispatch
│   │
│   ├── router/                # 转向指引进程
│   │   ├── router.h           # 路由器类（转向判定、路径推进）
│   │   └── router.cpp         # 入口：poll loop 监听 mq_route_exert
│   │
│   ├── mqtt_service/          # MQTT 通信服务
│   │   ├── CMakeLists.txt
│   │   ├── mqtt_client.h      # MQTT 客户端基类（基于 libmosquitto）
│   │   ├── mqtt_publisher.cpp # MQTT 发布端（从 mq_mqtt_publish 取消息发往从机）
│   │   └── mqtt_subscriber.cpp# MQTT 订阅端（接收从机事件，更新 SHM，触发调度）
│   │
│   ├── http_service/          # HTTP API 服务（FastCGI）
│   │   ├── CMakeLists.txt
│   │   ├── fcgi_utils.h       # FastCGI 工具函数
│   │   ├── Status.cpp         # GET /api/status — 读取 SHM 返回拓扑和车辆状态
│   │   ├── Task.cpp           # POST /api/assign, /api/cancel — 任务指派与取消
│   │   └── Topo.cpp           # POST /api/ban, /api/access, /api/repair — 拓扑编辑
│   │
│   └── log_daemon/            # 日志守护进程
│       ├── CMakeLists.txt
│       └── main.cpp           # 从 mq_log 读取日志消息并写入 systemd journal
│
├── tests/                      # 测试
│   ├── CMakeLists.txt
│   ├── test_logger.cpp        # 日志系统测试
│   ├── test_shm.cpp           # 共享内存测试
│   ├── test_secureExit.cpp    # 安全退出测试
│   ├── test_timer_fd.cpp      # 定时器测试
│   ├── test_1_http.cpp        # HTTP API 测试
│   ├── test_mq_mqtt.cpp       # MQ + MQTT 联调测试
│   ├── inject_log.cpp         # 日志注入测试
│   ├── test_tmp.cpp           # 临时测试
│   ├── mq/                    # MQ 通信测试
│   │   ├── CMakeLists.txt
│   │   ├── test_mq1.cpp       # MQ 测试端 1
│   │   └── test_mq2.cpp       # MQ 测试端 2
│
└── scripts/                    # 部署脚本
    ├── start_api.sh           # FastCGI 进程启动脚本
    ├── nginx.txt              # Nginx 配置参考
    ├── log-scripts.md         # 日志调试脚本
    └── secure-exit.md         # 安全退出机制说明
```

---

## 进程说明

### 核心进程

| 进程 | 入口 | 队列监听 | 职责 |
|------|------|----------|------|
| **model_manager** | `src/model_manager/` | — | 系统初始化：创建 SHM、写入初始拓扑和车辆数据、创建 MQ 队列 |
| **planner** | `src/planner/` | `mq_task_dispatch` | A* 路径规划，更新路径栈到 SHM，向 MQTT 发布转向指令 |
| **router** | `src/router/` | `mq_route_exert` | 处理到达事件，计算下一段路的方向，下发 ORIENT 指令 |
| **mqtt_subscriber** | `src/mqtt_service/` | — (MQTT)  | 接收 `car/+/event`，解析事件，更新 SHM 并触发调度 |
| **mqtt_publisher** | `src/mqtt_service/` | `mq_mqtt_publish` | 从 MQ 取 MQTT 发布消息，发给对应从机 |
| **log_daemon** | `src/log_daemon/` | `mq_log` | 收集各进程日志，写入 systemd journal |
| **Status** | `src/http_service/` | — (HTTP) | `GET /api/status` — 输出 SHM 数据的 JSON 快照 |
| **Task** | `src/http_service/` | — (HTTP) | `POST /api/assign|cancel` — 任务管理 |
| **Topo** | `src/http_service/` | — (HTTP) | `POST /api/ban|access|repair` — 拓扑管理 |

### POLL 事件循环

所有进程采用统一的 poll 主循环模式，无轮询、纯事件驱动：

```
             ┌─────────────────┐
             │   poll(-1)      │
             │  无限等待        │
             └────┬────────────┘
                  │
        ┌─────────┼──────────┐
        ▼                    ▼
   signalfd 可读          MQ fd 可读
        │                    │
        ▼                    ▼
  设置 g_shutdown         receive 消息
  或处理 SIGUSR1          dispatch 处理
```

---

## 共享内存数据结构

```
ShmLayout:
  ├── ShmHeader (64 B)       — 魔数、版本号、初始化标志
  ├── Seqlock map_lock (64 B) — MapData 顺序锁（读者用）
  ├── ProcMutex map_mtx (64 B)— MapData 写者互斥
  ├── MapData (~8 KB)        — 地图数据
  │   ├── nodes_[32]         — 节点数组
  │   ├── edges_[128]        — 边数组
  │   └── adj_[32]           — 邻接表
  ├── Seqlock car_lock (64 B) — CarData 顺序锁
  ├── ProcMutex car_mtx (64 B)— CarData 写者互斥
  └── CarData (~1 KB)        — 车辆状态
      └── cars_[2]           — 小车数组
```

---

## 构建与运行

### 依赖

- C++17 编译器 (gcc)
- CMake >= 3.16
- libmosquitto (MQTT 客户端)
- libfcgi (FastCGI)
- nlohmann-json (JSON 解析)
- POSIX RT (librt)

### 编译

```bash
cd master
mkdir build && cd build
cmake ..
make
```

### 启动顺序

1. **log_daemon** — 日志服务
2. **model_manager** — 初始化 SHM 和 MQ 队列
3. **planner** — 路径规划引擎
4. **router** — 转向指引
5. **mqtt_subscriber** + **mqtt_publisher** — MQTT 通信
6. **Nginx** + **FastCGI** 进程 — HTTP 服务

---

## 系统常量配置

见 `include/config/config.h`，关键可调参数：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `AGV_MAX_CARS` | 2 | 最大小车数量 |
| `AGV_MAX_NODES` | 32 | 最大节点数 |
| `AGV_MAX_EDGES` | 128 | 最大边数 |
| `AGV_MAX_PATHLEN` | 100 | 规划路径最大长度 |
| `AGV_MQTT_HOST` | "localhost" | MQTT Broker 地址 |

---

## API 文档

- **HTTP API**: 见 `system-api.md`
- **MQTT 协议**: 见 `mqtt-api.md`
