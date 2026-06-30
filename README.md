# ASI_Tm — AIS 数据组播/广播发送与接收

[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)]()
[![Language](https://img.shields.io/badge/C%2B%2B-20-blue)]()

一套跨平台的 **AIS（船舶自动识别系统）数据 UDP 组播/广播** 工具集，支持从原始数据文件中提取 NMEA 0183 格式的 AIS 语句，通过 IPv4 组播（IGMP）或广播方式发送到网络中，并提供配套的接收端进行验证。

---

## 项目结构

```
ASI_Tm/
├── main.cpp              # 发送端入口 (ASI_transmit)
├── AisTransmitter.h      # 发送端头文件
├── AisTransmitter.cpp    # Socket 创建 / AIS 提取 / 发送逻辑
├── mcast_receiver.cpp    # 组播接收端 (mcast_receiver)
├── CMakeLists.txt        # CMake 构建脚本
├── send_data.txt         # 示例 AIS 数据 (NMEA 0183 语句)
└── README.md
```

## 可执行文件

| 可执行文件 | 角色 | 说明 |
|-----------|------|------|
| `ASI_transmit` | **发送端** | 从文件提取 AIS 语句，通过 UDP 组播/广播发出 |
| `mcast_receiver` | **接收端** | 加入组播组，接收并显示 AIS 语句 |

---

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

程序通过正则表达式 `!AIVD[OM],[^\r\n]+`（[AisTransmitter.cpp:55](AisTransmitter.cpp#L55)）从输入文件中逐行提取 AIS 语句。

### 2. UDP（传输层）

使用 **UDP（User Datagram Protocol）** 而非 TCP，原因：

- AIS 数据是**实时、单向广播**类数据，不需要可靠传输
- 组播必须基于 UDP（TCP 不支持一对多）
- 低延迟，无连接建立开销

代码 ([AisTransmitter.cpp:83](AisTransmitter.cpp#L83) / [AisTransmitter.cpp:131](AisTransmitter.cpp#L131))：
```cpp
socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)   // IPv4 + 数据报 + UDP
```

### 3. IPv4 组播（网络层）

- 组播地址范围：`224.0.0.0 ~ 239.255.255.255`（D 类地址，前 4 位 = `1110`）
- [main.cpp:91-96](main.cpp#L91-L96)：程序自动检测目标 IP 首字节 `∈ [224, 239]` 来切换组播/广播模式
- **TTL（Time To Live）**：通过 `setsockopt(IPPROTO_IP, IP_MULTICAST_TTL, ttl)` 控制（[AisTransmitter.cpp:92-98](AisTransmitter.cpp#L92-L98)）
  - `TTL=1`：不跨路由器（同子网）
  - `TTL=16`：覆盖企业网
  - `TTL=64`：更大范围
  - `TTL=255`：全局可达（需组播路由协议配合）

### 4. IGMP（网络层 — 组管理协议）

**IGMP（Internet Group Management Protocol）** 负责在接收端主机和本地路由器之间传递组播组成员信息。本程序使用了两个关键 socket 选项：

| socket 选项 | 效果 | 代码位置 |
|-------------|------|----------|
| `IP_ADD_MEMBERSHIP` | 内核自动发出 **IGMP Membership Report**，通知路由器 | [mcast_receiver.cpp:109-119](mcast_receiver.cpp#L109-L119) |
| `IP_DROP_MEMBERSHIP` | 内核自动发出 **IGMP Leave**，退出组播组 | [mcast_receiver.cpp:159-160](mcast_receiver.cpp#L159-L160) |

#### IGMP 在网络中间设备上的两个关键机制：

- **IGMP Snooping（交换机）**：二层交换机"偷看" IGMP 报文，记录哪些端口加入了哪些组播组，从而只向相关端口转发组播数据帧，避免泛洪
- **IGMP Querier（路由器）**：路由器定期发送 Query 报文，确认子网内还有哪些主机在监听组播组

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

## 编译

### 前置条件

- CMake ≥ 3.16
- C++20 编译器（MSVC 2019+ / GCC 10+ / Clang 12+）

### 构建步骤

```bash
cd ASI_Tm
mkdir build && cd build
cmake ..
cmake --build .
```

Windows 下 CMake 会自动链接 `ws2_32.lib`。

---

## 使用方法

### 发送端 (`ASI_transmit`)

```bash
# 基本用法：将 send_data.txt 中的 AIS 语句发送到本地
ASI_transmit send_data.txt

# 发送到指定组播地址
ASI_transmit send_data.txt --host 239.192.0.1 --port 10110

# 广播发送（非组播地址）
ASI_transmit send_data.txt --host 192.168.1.255 --port 10110

# 带发送间隔 + 循环发送
ASI_transmit send_data.txt --host 239.192.0.1 --port 10110 --delay 100 --loop

# 设置组播 TTL（跨路由器跳数）
ASI_transmit send_data.txt --host 239.192.0.1 --ttl 16

# 同时写入文件
ASI_transmit send_data.txt --host 239.192.0.1 --output result.txt
```

#### 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `<输入文件>` | 包含 AIS 原始数据的文本文件 | 必填 |
| `--host <IP>` | 目标地址（组播 224~239 自动切换组播模式） | `127.0.0.1` |
| `--port <端口>` | 目标端口 | `10110` (AIS 常用端口) |
| `--delay <毫秒>` | 每条发送间隔 | `0` |
| `--ttl <跳数>` | 组播 TTL（同网段=1，企业网=16，更大范围=64） | `1` |
| `--output <文件>` | 同时输出到文件 | 无 |
| `--loop` | 循环发送模式 | 单次 |
| `--help` | 显示帮助 | — |

### 接收端 (`mcast_receiver`)

```bash
# 默认监听 239.192.0.1:10110
mcast_receiver

# 指定组播地址和端口
mcast_receiver 239.192.0.1 10110

# 监听其他组播组
mcast_receiver 224.0.0.1 5000
```

按 `Ctrl+C` 停止接收，程序会自动发送 IGMP Leave 退出组播组。

---

## 网络运行全流程

整个系统由**发送端 (`ASI_transmit`)** + **网络设备（交换机/路由器）** + **接收端 (`mcast_receiver`)** 三部分组成。以下按时间顺序描述一次完整的组播数据收发过程。

---

### 阶段一：接收端启动 & 加入组播组 (`mcast_receiver`)

```
接收端主机
────────────

步骤 1 ─ 创建 UDP socket
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        │  作用: 创建一个 IPv4 UDP 数据报套接字
        │  协议: UDP over IPv4
        ▼
步骤 2 ─ 端口复用
        setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
        │  作用: 允许多个接收端进程同时监听同一端口
        │  协议: UDP (端口复用是 UDP 的常见用法)
        ▼
步骤 3 ─ 绑定到全零地址 (INADDR_ANY)
        bind(sock, 0.0.0.0:10110)
        │  作用: 告诉协议栈——所有网卡收到的、目的端口为 10110 的 UDP 包都给我
        │  协议: UDP + IP
        ▼
步骤 4 ─ ★ 加入组播组 (核心步骤)
        setsockopt(IPPROTO_IP, IP_ADD_MEMBERSHIP, {mcast=239.192.0.1, iface=INADDR_ANY})
        │
        ├── 内核动作 (a): 发送 IGMP Membership Report 报文到局域网
        │       dst=239.192.0.1, IGMP type=0x16 (Membership Report v2)
        │       → 本地路由器收到后更新 IGMP 表
        │       → 如果有 PIM 等组播路由协议，会向上游路由器传播 Join
        │
        ├── 内核动作 (b): 配置网卡 MAC 过滤寄存器
        │       计算组播 MAC: 239.192.0.1 → 01:00:5E:40:00:01
        │       网卡硬件层开始接收 dst_mac=01:00:5E:40:00:01 的帧
        │
        └── 交换机动作 (IGMP Snooping):
                交换机看到 IGMP Report 报文后，记录: "端口 X 加入了组 239.192.0.1"
                后续该组的组播流量只向端口 X 转发
        ▼
步骤 5 ─ 进入接收循环
        recvfrom(sock, buf, ...)  ← 阻塞等待
```

---

### 阶段二：发送端发送数据 (`ASI_transmit`)

```
发送端主机
────────────

步骤 1 ─ 读取文件 & 提取 AIS 语句
        用正则 !AIVD[OM],[^\r\n]+ 从输入文件逐行提取 NMEA 0183 语句
        协议: NMEA 0183 / AIS (应用层)

步骤 2 ─ 判断目标地址类型
        if (目标IP首字节 ∈ [224, 239])  →  组播模式
        else                           →  广播/单播模式
        协议: IPv4 寻址 (网络层)

步骤 3 ─ 创建 Socket (以组播为例)
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        协议: UDP over IPv4

步骤 4 ─ 配置 Socket 选项
        setsockopt(IPPROTO_IP, IP_MULTICAST_TTL, ttl)
        │  作用: 设置 IP 包头的 TTL 字段，控制跨路由器跳数
        │  协议: IP Multicast (网络层)
        │
        setsockopt(SOL_SOCKET, SO_BROADCAST, 1)
        │  作用: 允许发送广播（兼容模式）
        │
        setsockopt(IPPROTO_IP, IP_MULTICAST_IF, iface)  [可选]
           作用: 多网卡时指定出站网卡

步骤 5 ─ 逐条发送 (sendto)
        sendto(sock, "!AIVDM,...", len, 0, {dst=239.192.0.1, port=10110}, sizeof(addr))

        这个调用在内核中触发以下封装过程:
        │
        ▼
        ┌── 应用层 ────────────────────────────────────┐
        │ 数据:  "!AIVDM,1,1,,A,..."                   │  NMEA 0183 明文
        └────────────────┬─────────────────────────────┘
                         │
                         ▼
        ┌── 传输层 ────────────────────────────────────┐
        │ UDP 头:  src_port=ephemeral  dst_port=10110   │  UDP 协议 (用户态→内核)
        │ 负载:    "!AIVDM,1,1,,A,..."                  │
        │ 校验和:  可选                                 │
        └────────────────┬─────────────────────────────┘
                         │
                         ▼
        ┌── 网络层 ────────────────────────────────────┐
        │ IP 头:   src=本机IP  dst=239.192.0.1          │  IPv4 Multicast
        │          protocol=17(UDP)  TTL=1              │  D 类地址
        │ 负载:    [UDP 头 + AIS 数据]                   │
        └────────────────┬─────────────────────────────┘
                         │
                         ▼
        ┌── 链路层 ────────────────────────────────────┐
        │ MAC 头:  dst=01:00:5E:40:00:01               │  Ethernet
        │          src=本机网卡MAC                       │  组播 MAC 映射
        │          EtherType=0x0800(IPv4)               │
        │ 负载:    [IP 头 + UDP 头 + AIS 数据]           │
        │ FCS:     CRC32                                │
        └────────────────┬─────────────────────────────┘
                         │
                         ▼
                    帧从网卡物理层发出 (PHY)
```

---

### 阶段三：网络设备转发

```
                    交换机 (支持 IGMP Snooping)
                    ──────────────────────────────

帧到达交换机端口 ──→ 查 MAC 地址表 + IGMP Snooping 表
                    │
                    ├── 如果目标 MAC 是组播 MAC (01:00:5E:xx:xx:xx):
                    │      IGMP Snooping 表查询 → 哪些端口加入了这个组?
                    │      ├── 有记录 → 只向这些端口转发 ✓ (不泛洪)
                    │      └── 无记录 → 按配置处理 (泛洪到同 VLAN / 丢弃)
                    │
                    └── 如果是普通单播 MAC:
                          MAC 地址表查询 → 精确端口转发


                    路由器 (支持 IGMP Querier + PIM)
                    ────────────────────────────────

如果 TTL > 1 且需要跨网段:

组播包到达路由器 ──→ TTL 减 1 → 如果 TTL == 0 → 丢弃
                               │
                               └── TTL > 0 → 查组播路由表 (PIM/Multicast Routing)
                                               确定出接口列表
                                               每个出接口复制一份发出
```

---

### 阶段四：接收端收到数据 (`mcast_receiver`)

```
接收端主机
────────────

帧从网卡进入:
  │
  ▼
步骤 1 ─ 硬件 MAC 过滤 (网卡芯片)
        检查 dst_mac 是否匹配已注册的组播 MAC 列表
        ├── 不匹配 → 丢弃 (硬件层，CPU 无感知)
        └── 匹配   → 接收帧，触发中断通知驱动
  │
  ▼
步骤 2 ─ IP 层处理
        检查 dst_ip:
        ├── dst_ip == 239.192.0.1 → 匹配已加入的组播组 ✓
        ├── TTL == 0             → 丢弃
        └── 校验和错误            → 丢弃
        提取 protocol 字段 = 17(UDP)，递交给 UDP 层
  │
  ▼
步骤 3 ─ UDP 层处理
        检查 dst_port == 10110:
        ├── 匹配 bind(0.0.0.0:10110) → 找到监听 socket ✓
        └── 没有 socket 监听          → 回复 ICMP Port Unreachable
        通过 socket 缓冲区将数据递交给用户态进程
  │
  ▼
步骤 4 ─ 用户态 recvfrom() 返回
        recvfrom(buf) 解除阻塞
        打印: [序号] 来源IP:端口 → !AIVDM,1,1,,A,...
  │
  ▼
步骤 5 ─ 回到接收循环 → recvfrom() 阻塞等待下一条
```

---

### 退出流程：接收端离开组播组

```
接收端按 Ctrl+C
  │
  ▼
setsockopt(IPPROTO_IP, IP_DROP_MEMBERSHIP, {mcast=239.192.0.1})
  │
  ├── 内核发送 IGMP Leave 报文到局域网
  │       dst=224.0.0.2 (All Routers), IGMP type=0x17 (Leave v2)
  │
  ├── 路由器更新 IGMP 表 → 发送 Group-Specific Query 确认是否还有其他成员
  │       → 无其他成员回复 → 停止向该子网转发该组流量
  │
  ├── 交换机 IGMP Snooping 看到 Leave → 从 Snooping 表中删除该端口
  │       → 后续该组的帧不再向该端口转发
  │
  └── 网卡移除 MAC 过滤项 → 不再接收 01:00:5E:40:00:01 的帧
  │
  ▼
closeSocket(sock) → WSACleanup() → 进程退出
```

---

### 完整流程图（端到端）

```
发送端 (ASI_transmit)                 交换机                    接收端 (mcast_receiver)
─────────────────────              ────────────                ─────────────────────────

                                                                 ┌───────── ① socket(UDP)
                                                                 ├───────── ② SO_REUSEADDR
                                                                 ├───────── ③ bind(0:10110)
                                                                 ├───────── ④ IP_ADD_MEMBERSHIP
                                                                 │           └→ IGMP Report → 交换机记表
                                                                 ├───────── ⑤ recvfrom() 阻塞
                                                                 │
① extractAisSentences(file) ─→ AIS 语句列表                       │
   协议: NMEA 0183                                                │
② socket(UDP)                                                    │
③ setsockopt(TTL)                                                │
   协议: IP Multicast                                             │
④ sendto("!AIVDM,..." → 239.192.0.1:10110)                       │
   协议: UDP → IP → Ethernet                                      │
           │                                                      │
           ▼                                                      │
⑤ IP 封装: dst=239.192.0.1 ──────────→ ⑥ 收到帧                  │
           src=本机                     IGMP Snooping 查表        │
           TTL=1                        端口 X 在组中 ✓           │
           │                            定向转发 ──────────────→ ⑦ 帧到达网卡
           ▼                                                         MAC=01:00:5E:40:00:01 匹配 ✓
⑥ MAC 封装:                                                                 │
   dst=01:00:5E:40:00:01                                               ⑧ IP 层: dst=239.192.0.1 ✓
   物理层发送                                                               │
                                                                       ⑨ UDP: dst_port=10110 ✓
                                                                           │
                                                                       ⑩ recvfrom() 返回
                                                                          打印 AIS 语句
                                                                           │
                                                                       ⑪ 循环 → recvfrom() 再次阻塞
                                                                           │
发送端退出                                                             用户按 Ctrl+C
                                                                           │
                                                                       ⑫ IP_DROP_MEMBERSHIP
                                                                          └→ IGMP Leave → 交换机删表
                                                                           │
                                                                       ⑬ closeSocket() → 退出
```

### 组播 vs 广播的关键区别

| | 组播 (Multicast) | 广播 (Broadcast) |
|---|---|---|
| **地址范围** | `224.0.0.0/4` | `x.x.x.255` / `255.255.255.255` |
| **接收方式** | 主动加入 (IGMP Join) | 被动接收 |
| **交换机行为** | IGMP Snooping，定向转发 | 泛洪到所有端口 |
| **带宽效率** | 高（只发给订阅者） | 低（全子网泛洪） |
| **跨网段** | 通过 PIM 等组播路由协议 | 不可跨路由器 |

---

## AIS 数据格式

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

## 技术要点

1. **自动模式切换**：根据目标 IP 首字节自动判断组播（224~239）或广播/单播模式
2. **跨平台**：Windows 使用 Winsock2，Linux 使用 POSIX socket，通过条件编译统一接口
3. **RAII Winsock 管理**：静态 guard 对象确保 `WSAStartup` / `WSACleanup` 配对
4. **端口复用**：接收端使用 `SO_REUSEADDR`，允许多个接收进程同时监听
5. **TTL 控制**：通过 `IP_MULTICAST_TTL` 限制组播包跨路由器的跳数

---

## License

MIT
