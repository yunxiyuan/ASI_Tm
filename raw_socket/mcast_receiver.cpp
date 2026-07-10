/**
 * mcast_receiver - UDP 组播接收端
 *
 * 用法:  mcast_receiver [组播地址] [端口]
 *
 * 默认:  mcast_receiver 239.192.0.1 10110
 *
 * 学习要点：
 *   1. IP_ADD_MEMBERSHIP → 发送 IGMP Membership Report，告诉交换机"我要收这个组"
 *   2. SO_REUSEADDR      → 多个接收端可同时监听同一端口
 *   3. bind(INADDR_ANY)  → 接收所有网卡上的数据
 *
 * 编译方法 (命令行):
 *   cl /EHsc /std:c++20 /utf-8 mcast_receiver.cpp /Fe:mcast_receiver.exe ws2_32.lib
 */

#include <iostream>
#include <cstring>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    #define closeSocket closesocket
    typedef SOCKET SocketType;
    const SocketType INVALID_SOCK = INVALID_SOCKET;
#else
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define closeSocket close
    typedef int SocketType;
    const SocketType INVALID_SOCK = -1;
#endif
#include <string>

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    const char* mcast_addr = "239.192.0.1";
    int         port       = 10110;

    if (argc >= 2) mcast_addr = argv[1];
    if (argc >= 3) port       = std::stoi(argv[2]);

    std::cout << "=== 组播接收端 ===\n"
              << "组播组: " << mcast_addr << "\n"
              << "端口:   " << port << "\n\n";

    // ── Winsock 初始化 (Windows only) ──
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[错误] WSAStartup 失败\n";
        return 1;
    }
#endif

    // ── ① 创建 UDP socket ──
    SocketType sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK) {
        std::cerr << "[错误] socket 失败\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // ── ② 端口复用（多个接收端同时监听不冲突） ──
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse),
                   sizeof(reuse)) != 0) {
        std::cerr << "[错误] SO_REUSEADDR 失败\n";
    }

    // ── ③ bind 到 INADDR_ANY ──
    //     告诉协议栈：任何网卡收到目的端口 = port 的 UDP 包都给我
    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(static_cast<u_short>(port));
    local.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        std::cerr << "[错误] bind 端口 " << port << " 失败\n";
        closeSocket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // ── ④ 加入组播组 ★ 这是广播和组播的核心区别 ──
    //     执行后内核会：
    //       a) 发 IGMP Membership Report 给本网段路由器
    //       b) 配置网卡接收 dst_mac = 01:00:5E:xx:xx:xx 的帧
    //       c) 交换机通过 IGMP snooping 知道本端口加入了这个组
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(mcast_addr);   // 想听哪个组
    mreq.imr_interface.s_addr = INADDR_ANY;              // 从哪个网卡加入
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq),
                   sizeof(mreq)) != 0) {
        std::cerr << "[错误] 加入组播组 " << mcast_addr << " 失败\n"
                  << "        (检查组播地址格式: 239.x.x.x)\n";
        closeSocket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[就绪] 等待组播数据... (按 Ctrl+C 停止)\n"
              << "----------------------------------------\n";

    // ── ⑤ 接收循环 ──
    char buf[65536];
    int  count = 0;
    while (true) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = sizeof(from);
#else
        socklen_t fromLen = sizeof(from);
#endif
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                          reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 0) {
            buf[n] = '\0';
            count++;
            char srcIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, srcIP, sizeof(srcIP));
            std::cout << "[" << count << "] " << srcIP
                      << ":" << ntohs(from.sin_port) << " → "
                      << buf << std::endl;
        } else if (n == 0) {
            std::cout << "[信息] 发送端关闭\n";
            break;
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            std::cerr << "[错误] recvfrom 失败, 错误码: " << err << "\n";
#else
            std::cerr << "[错误] recvfrom 失败\n";
#endif
            break;
        }
    }

    // ── ⑥ 离开组播组 (进程退出时内核自动发送 Leave) ──
    setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
               reinterpret_cast<const char*>(&mreq), sizeof(mreq));

    closeSocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
