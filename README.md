# ASI_Tm — AIS 数据组播/广播发送与接收

[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)]()
[![Language](https://img.shields.io/badge/C%2B%2B-20-blue)]()

一套跨平台的 **AIS（船舶自动识别系统）数据 UDP 组播/广播** 工具集，支持从原始数据文件中提取 NMEA 0183 格式的 AIS 语句，通过 IPv4 组播（IGMP）或广播方式发送到网络中，并提供配套的接收端进行验证。

项目提供 **两个版本** 的实现，方便对比学习：

| 版本 | 目录 | 说明 |
|---|---|---|
| 裸 Socket 版本 | `raw_socket/` | 原始 UDP socket 实现，直接 sendto/recvfrom |
| gRPC 版本 | `grpc_version/` | 使用 protobuf + gRPC 服务端流替代裸 socket |

---

## 项目结构

```
ASI_Tm/
├── data/
│   └── 2025_10_28_13_57_37_450RawData2.txt  # 原始串口数据
├── raw_socket/                                # 裸 Socket 版本
│   ├── main.cpp                               # 发送端入口 (ASI_transmit)
│   ├── AisTransmitter.h                       # 发送端头文件
│   ├── AisTransmitter.cpp                     # Socket 创建 / AIS 提取 / 发送逻辑
│   ├── mcast_receiver.cpp                     # 组播接收端 (mcast_receiver)
│   └── CMakeLists.txt
├── grpc_version/                              # gRPC 版本
│   ├── ais.proto                              # protobuf 协议定义
│   ├── ais_server.cc                          # gRPC 服务端（提取文件 + 流式发送）
│   ├── ais_client.cc                          # gRPC 客户端（接收并显示）
│   ├── log_wrapper.h/.cc                      # log4cplus 日志封装
│   ├── log4cplus.properties                   # 日志配置（级别/格式/输出目的地）
│   └── CMakeLists.txt
└── README.md
```

---

## 版本对比

| | raw_socket | grpc_version |
|---|---|---|
| 传输方式 | `sendto()` UDP | HTTP/2 TCP |
| 数据格式 | 原始 NMEA ASCII 字符串 | protobuf 二进制序列化 |
| 错误处理 | `return false` + cerr | `grpc::Status` 错误码 |
| 代码量 | ~317 行 | ~100 行业务 + 13 行 proto |
| 跨语言 | 需每种语言自己实现解析 | 一份 proto 生成 12 种语言代码 |
| 消息边界 | UDP 天然边界 | HTTP/2 帧 |
| 流控 | 无 | HTTP/2 原生流量控制 |

---

# 一、raw_socket 版本

## 协议栈（共涉及 5 个协议）

本程序从应用到链路层共使用 **5 个协议**，每一层协议都有明确的代码对应：

```
┌─────────────────────────────────────────────────────────────────────────┐
│ 层级       │ 协议               │ 用途              │ 代码直接证据       │
├───────────┼───────────────────┼──────────────────┼────────────────────┤
│ 应用层     │ NMEA 0183 (AIS)   │ 船舶定位数据格式   │ regex: !AIVD[OM]   │
│ 传输层     │ UDP               │ 无连接数据报传输    │ SOCK_DGRAM,        │
│           │                   │                   │ IPPROTO_UDP        │
│ 网络层-1   │ IPv4 Multicast    │ 一对多组播寻址      │ 224.0.0.0/4, TTL  │
│ 网络层-2   │ IGMP v2/v3        │ 组播组成员管理      │ IP_ADD_MEMBERSHIP  │
│           │                   │                   │ IP_DROP_MEMBERSHIP │
│ 链路层     │ Ethernet          │ MAC 层组播帧过滤   │ 01:00:5E:xx:xx:xx │
└───────────┴───────────────────┴──────────────────┴────────────────────┘
```

### 1. NMEA 0183 / AIS（应用层）

**AIS（Automatic Identification System）** 是国际海事组织强制要求的船舶自动识别系统。AIS 数据使用 **NMEA 0183** 文本格式传输。

- `!AIVDM` — 接收自其他船舶的数据（**V**HF Data-link **M**essage）
- `!AIVDO` — 本船自身广播的数据（**V**HF Data-link **O**wn vessel）

程序通过正则表达式 `!AIVD[OM],[^\r\n]+`（[AisTransmitter.cpp:55](raw_socket/AisTransmitter.cpp#L55)）从输入文件中逐行提取 AIS 语句。

### 2. UDP（传输层）

使用 **UDP（User Datagram Protocol）** 而非 TCP，原因：

- AIS 数据是**实时、单向广播**类数据，不需要可靠传输
- 组播必须基于 UDP（TCP 不支持一对多）
- 低延迟，无连接建立开销

代码 ([AisTransmitter.cpp:83](raw_socket/AisTransmitter.cpp#L83) / [AisTransmitter.cpp:131](raw_socket/AisTransmitter.cpp#L131))：
```cpp
socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)   // IPv4 + 数据报 + UDP
```

### 3. IPv4 组播（网络层）

- 组播地址范围：`224.0.0.0 ~ 239.255.255.255`（D 类地址，前 4 位 = `1110`）
- [main.cpp:91-96](raw_socket/main.cpp#L91-L96)：程序自动检测目标 IP 首字节 `∈ [224, 239]` 来切换组播/广播模式
- **TTL（Time To Live）**：通过 `setsockopt(IPPROTO_IP, IP_MULTICAST_TTL, ttl)` 控制（[AisTransmitter.cpp:92-98](raw_socket/AisTransmitter.cpp#L92-L98)）
  - `TTL=1`：不跨路由器（同子网）
  - `TTL=16`：覆盖企业网
  - `TTL=64`：更大范围
  - `TTL=255`：全局可达（需组播路由协议配合）

### 4. IGMP（网络层 — 组管理协议）

**IGMP（Internet Group Management Protocol）** 负责在接收端主机和本地路由器之间传递组播组成员信息。本程序使用了两个关键 socket 选项：

| socket 选项 | 效果 | 代码位置 |
|-------------|------|----------|
| `IP_ADD_MEMBERSHIP` | 内核自动发出 **IGMP Membership Report**，通知路由器 | [mcast_receiver.cpp:109-119](raw_socket/mcast_receiver.cpp#L109-L119) |
| `IP_DROP_MEMBERSHIP` | 内核自动发出 **IGMP Leave**，退出组播组 | [mcast_receiver.cpp:159-160](raw_socket/mcast_receiver.cpp#L159-L160) |

### 5. Ethernet（链路层）

IPv4 组播地址会按 **IANA 规范** 映射为以太网组播 MAC 地址：

```
IPv4 组播地址:    239.192.0.1
                   │  └──────┬──────┘
                   │      低 23 位
                   └─── 不参与映射（高 5 位丢失）
                        ↓
MAC 组播地址:     01:00:5E:40:00:01
                  └─┬─┘ └───┬────┘
                固定前缀   取自 IP 低 23 位
```

接收端网卡在硬件层过滤帧：只接收 `dst_mac == 01:00:5E:*` 的帧，其余丢弃。

---

## 编译与运行 (raw_socket)

### 前置条件

- CMake ≥ 3.16
- C++20 编译器（MSVC 2019+ / GCC 10+ / Clang 12+）

### 构建

```bash
cd raw_socket
mkdir build && cd build
cmake ..
make -j1
```

Windows 下 CMake 会自动链接 `ws2_32.lib`。

### 发送端

```bash
# 基本用法
./ASI_transmit ../../data/2025_10_28_13_57_37_450RawData2.txt

# 指定组播地址
./ASI_transmit ../../data/2025_10_28_13_57_37_450RawData2.txt --host 239.192.0.1 --port 10110

# 带发送间隔 + 循环
./ASI_transmit ../../data/2025_10_28_13_57_37_450RawData2.txt --host 239.192.0.1 --port 10110 --delay 100 --loop
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `<输入文件>` | AIS 原始数据文件 | 必填 |
| `--host <IP>` | 目标地址（224~239 自动切换组播） | `127.0.0.1` |
| `--port <端口>` | 目标端口 | `10110` |
| `--delay <ms>` | 发送间隔 | `0` |
| `--ttl <跳数>` | 组播 TTL | `1` |
| `--output <文件>` | 同时写入文件 | 无 |
| `--loop` | 循环发送 | 单次 |

### 接收端

```bash
# 默认 239.192.0.1:10110
./mcast_receiver

# 指定地址和端口
./mcast_receiver 239.192.0.1 10110
```

---

# 二、gRPC 版本

使用 **protobuf** 定义协议 + **gRPC 服务端流** 替代裸 socket，底层自动处理序列化、传输、错误码。

## 协议定义 ([ais.proto](grpc_version/ais.proto))

```protobuf
service AisService {
  rpc StreamAisSentences (AisRequest) returns (stream AisSentence) {}
}

message AisRequest {
  string filter = 1;     // 过滤条件，空=全部
  int32 max_count = 2;   // 最多发多少条，0=全部
}

message AisSentence {
  int32 index = 1;        // 序号
  string payload = 2;     // NMEA 语句内容
  int64 timestamp_ms = 3; // 发送时间戳
}
```

**为什么用服务端流？** AIS 数据是"一对多持续推送"场景，客户端发一次请求，服务端持续推送所有语句，天然适合 Server Streaming 模式。

## 编译与运行 (grpc_version)

### 前置条件

- CMake ≥ 3.16
- C++17 编译器
- protobuf + gRPC 已安装（`$HOME/.local` 或系统路径）
- **log4cplus 2.x** 已编译安装（见下文"编译安装 log4cplus"）

### 编译安装 log4cplus（动态库 .so）

grpc_version 用 [log4cplus](https://github.com/log4cplus/log4cplus) 做日志。需先编译安装 2.x 版本（3.x 要求 C++23，不兼容本项目的 C++17）：

```bash
git clone --recurse-submodules https://github.com/log4cplus/log4cplus.git -b REL_2_2_0_1
mkdir build_log4cplus && cd build_log4cplus
cmake ../log4cplus -DBUILD_SHARED_LIBS=ON -DLOG4CPLUS_BUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=$HOME/log4cplus_install
cmake --build . -j"$(nproc)"
cmake --install .
```

> **注意**：log4cplus 的 build 目录必须放在本地 ext4 磁盘，不能放 VMware 共享文件夹（hgfs/NTFS 不支持 Linux symlink，链接 .so 会报 `Operation not supported`）。

### 构建

```bash
cd grpc_version
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=$HOME/.local -Dlog4cplus_DIR=$HOME/log4cplus_install/lib/cmake/log4cplus ..
make -j1
```

### 运行

**终端 1 — 服务端**：
```bash
./ais_server ../../data/2025_10_28_13_57_37_450RawData2.txt
# 输出(带时间戳/级别/logger名):
#   11:33:20 INFO  [grpc.server] 从文件提取到 4913 条 AIS 语句
#   11:33:20 INFO  [grpc.server] gRPC AIS 服务端监听 0.0.0.0:50051
```

**终端 2 — 客户端**：
```bash
./ais_client
# 基本用法：接收全部 4913 条

./ais_client localhost:50051 "" 10
# 只接收前 10 条
```

日志同时输出到控制台和 `logs/asi.log`（滚动文件，5MB/个，保留 3 个备份）。

| 参数 | 说明 | 默认值 |
|---|---|---|
| `<地址:端口>` | 服务端地址 | `localhost:50051` |
| `<filter>` | 过滤关键词 | 空（全部） |
| `<max_count>` | 最多接收条数 | 0（全部） |

### 日志系统（log4cplus）

grpc_version 用 log4cplus 替代 `std::cout`/`std::cerr` 做结构化日志，核心组件：

- **[log_wrapper.h](grpc_version/log_wrapper.h) / [log_wrapper.cc](grpc_version/log_wrapper.cc)**：封装日志初始化与命名 logger。`serverLogger()` 返回 `grpc.server`，`clientLogger()` 返回 `grpc.client`。全局 `Initializer` 保证生命周期。
- **[log4cplus.properties](grpc_version/log4cplus.properties)**：日志配置。改这一文件可调级别/格式/输出，无需重编译。

日志级别（升序）：`TRACE < DEBUG < INFO < WARN < ERROR < FATAL`。各级别用法见代码：

| 级别 | 用途 | 代码示例 |
|---|---|---|
| ERROR | 文件打不开、RPC 失败 | [ais_server.cc:29](grpc_version/ais_server.cc#L29) |
| WARN | 配置文件缺失（回退默认） | [log_wrapper.cc:22](grpc_version/log_wrapper.cc#L22) |
| INFO | 关键流程节点（提取条数、监听就绪） | [ais_server.cc:41](grpc_version/ais_server.cc#L41) |
| DEBUG | 逐条明细（客户端接收） | [ais_client.cc:54](grpc_version/ais_client.cc#L54) |

**运行时调级别**（改配置不重编译）：编辑 `log4cplus.properties` 第 5 行
```properties
log4cplus.rootLogger=DEBUG, CONSOLE, FILE   # 改 DEBUG→WARN 即可过滤 DEBUG/INFO
```
改完重启程序生效。生产建议 `INFO`（够定位问题、不刷屏），调试用 `DEBUG`（全看明细）。

---

## 数据格式

输入文件中的每一行可能包含多个字段，程序通过正则 `!AIVD[OM],[^\r\n]+` 提取有效的 NMEA 0183 语句：

```
!AIVDM,1,1,,A,C69N>L@00b;O0I5U;3bgf<P0<2L>4OSSe11111111110@Ql>T400,0*5F
│      │  │ │ │                                                      │
│      │  │ │ │                                                      └─ NMEA 校验和
│      │  │ │ └─ 频道 (A/B)
│      │  │ └─── 序列号
│      │  └────── 总片段数
│      └───────── 当前片段号
└──────────────── 语句类型: !AIVDM (其他船) / !AIVDO (本船)
```

---

## License

MIT
