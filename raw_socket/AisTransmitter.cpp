#include "AisTransmitter.h"

#include <fstream>
#include <iostream>
#include <regex>

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#define closesocket close
#undef  SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

// ============================================================
// Winsock 初始化 / 清理 (RAII)
// ============================================================
namespace {
    struct WinsockGuard {
        bool ok = false;
        WinsockGuard() {
#ifdef _WIN32
            WSADATA wsa;
            ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#else
            ok = true;
#endif
        }
        ~WinsockGuard() {
#ifdef _WIN32
            if (ok) WSACleanup();
#endif
        }
    };

    WinsockGuard& guard() {
        static WinsockGuard g;
        return g;
    }
} // namespace

// ============================================================
// 提取 AIS 语句
// ============================================================
std::vector<std::string> extractAisSentences(const std::string& filepath) {
    std::vector<std::string> sentences;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[错误] 无法打开文件: " << filepath << std::endl;
        return sentences;
    }

    std::regex pattern(R"(!AIVD[OM],[^\r\n]+)");

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        std::smatch match;
        if (std::regex_search(line, match, pattern)) {
            sentences.push_back(match.str());
        }
    }

    std::cout << "[信息] 从文件提取到 " << sentences.size()
              << " 条 AIS 语句 (共 " << lineNum << " 行)" << std::endl;
    return sentences;
}

// ============================================================
// 创建 UDP 组播 Socket (与广播的区别：设置 TTL)
// ============================================================
SocketType createMulticastSocket(const std::string& host, int port,
                                  int ttl,
                                  const std::string& mcast_iface_ip) {
    if (!guard().ok) {
        std::cerr << "[错误] Winsock 未初始化" << std::endl;
        return INVALID_SOCK;
    }

    SocketType sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK) {
        std::cerr << "[错误] 创建 socket 失败" << std::endl;
        return INVALID_SOCK;
    }

    // ① 组播 TTL — 控制跨越路由器的跳数
    //    TTL=1 (默认) → 不出本网段 → 学习阶段刚好
    //    TTL=16        → 覆盖典型企业内网
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
                   reinterpret_cast<const char*>(&ttl),
                   sizeof(ttl)) != 0) {
        std::cerr << "[错误] 设置 IP_MULTICAST_TTL 失败" << std::endl;
        closesocket(sock);
        return INVALID_SOCK;
    }

    // ② 指定出站网卡（可选，多网卡时有用）
    if (!mcast_iface_ip.empty()) {
        struct in_addr iface;
        iface.s_addr = inet_addr(mcast_iface_ip.c_str());
        if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
                       reinterpret_cast<const char*>(&iface),
                       sizeof(iface)) != 0) {
            std::cerr << "[警告] 设置 IP_MULTICAST_IF 失败" << std::endl;
        }
    }

    // ③ 也允许发送广播（兼容，不影响组播行为）
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&broadcast),
               sizeof(broadcast));

    std::cout << "[信息] UDP 组播 socket 就绪 -> " << host << ":" << port
              << "  [TTL=" << ttl << "]" << std::endl;
    return sock;
}

// ============================================================
// 创建 UDP 广播 Socket
// ============================================================
SocketType createBroadcastSocket(const std::string& host, int port) {
    if (!guard().ok) {
        std::cerr << "[错误] Winsock 未初始化" << std::endl;
        return INVALID_SOCK;
    }

    SocketType sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK) {
        std::cerr << "[错误] 创建 socket 失败" << std::endl;
        return INVALID_SOCK;
    }

    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                   reinterpret_cast<const char*>(&broadcast),
                   sizeof(broadcast)) != 0) {
        std::cerr << "[错误] 设置 SO_BROADCAST 失败" << std::endl;
        closesocket(sock);
        return INVALID_SOCK;
    }

    std::cout << "[信息] UDP socket 就绪 -> " << host << ":" << port
              << std::endl;
    return sock;
}

// ============================================================
// UDP 发送
// ============================================================
bool broadcastMessage(SocketType sock, const std::string& host, int port,
                      const std::string& message) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    int ret = sendto(sock, message.c_str(),
                     static_cast<int>(message.length()), 0,
                     reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    if (ret == SOCKET_ERROR) {
#ifdef _WIN32
        std::cerr << "[错误] sendto 失败, 错误码: " << WSAGetLastError()
                  << std::endl;
#else
        std::cerr << "[错误] sendto 失败" << std::endl;
#endif
        return false;
    }
    return true;
}

// ============================================================
// 关闭 Socket
// ============================================================
void closeSocket(SocketType sock) {
    if (sock != INVALID_SOCK) {
        closesocket(sock);
    }
}
