#pragma once

// 必须在所有 Windows 头文件之前定义
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX             // 禁止 windows.h 定义 min/max 宏
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <string>
#include <vector>

// 跨平台 socket 类型
#ifdef _WIN32
typedef SOCKET SocketType;
const SocketType INVALID_SOCK = INVALID_SOCKET;
#else
typedef int SocketType;
const SocketType INVALID_SOCK = -1;
#endif

// 提取 AIS 语句
std::vector<std::string> extractAisSentences(const std::string& filepath);

// 创建 UDP 广播 socket (设置 SO_BROADCAST)
SocketType createBroadcastSocket(const std::string& host, int port);

// 创建 UDP 组播 socket (设置 IP_MULTICAST_TTL + 可选出站接口)
// ttl: 跳数限制 (1=同网段, 16=企业网, 64=更大范围)
// mcast_iface_ip: 出站网卡 IP (空串 = 由系统选择)
SocketType createMulticastSocket(const std::string& host, int port,
                                  int ttl = 1,
                                  const std::string& mcast_iface_ip = "");

// 发送一条消息
bool broadcastMessage(SocketType sock, const std::string& host, int port,
                      const std::string& message);

// 关闭 socket
void closeSocket(SocketType sock);
