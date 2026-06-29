/**
 * ASI_transmit - AIS 数据提取与 UDP 广播发送
 *
 * 用法:  ASI_transmit <输入文件> [选项]
 *
 * 选项:
 *   --host <IP>      目标地址   (默认: 127.0.0.1)
 *                    组播地址: 239.x.x.x 或 224.x.x.x 自动切换组播模式
 *   --port <端口>    目标端口   (默认: 10110)
 *   --delay <毫秒>   发送间隔   (默认: 0)
 *   --ttl <跳数>     组播 TTL   (默认: 1, 仅组播模式生效)
 *   --output <文件>  同时写入文件
 *   --loop           循环发送
 */

#include "AisTransmitter.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

static void printHelp(const char* prog) {
    std::cout
        << "ASI_transmit - AIS 数据提取与 UDP 发送\n\n"
        << "用法:  " << prog << " <输入文件> [选项]\n\n"
        << "选项:\n"
        << "  --host <IP>      目标地址     (默认: 127.0.0.1)\n"
        << "                    组播: 224.0.0.0~239.255.255.255 自动切换组播\n"
        << "  --port <端口>    目标端口     (默认: 10110)\n"
        << "  --delay <毫秒>   每条间隔     (默认: 0)\n"
        << "  --ttl <跳数>     组播 TTL     (默认: 1, 组播模式生效)\n"
        << "  --output <文件>  写入文件     (可选)\n"
        << "  --loop           循环发送\n"
        << "  --help           帮助\n\n"
        << "AIS UDP 常用端口: 10110\n"
        << "组播默认 TTL=1(同网段) 16(企业网) 64(更大范围)\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (argc < 2) { printHelp(argv[0]); return 1; }

    std::string filepath, host = "127.0.0.1", outputPath;
    int port = 10110, delayMs = 0, ttl = 1;
    bool loop = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { printHelp(argv[0]); return 0; }
        else if (a == "--host"   && i + 1 < argc) host      = argv[++i];
        else if (a == "--port"   && i + 1 < argc) port      = std::stoi(argv[++i]);
        else if (a == "--delay"  && i + 1 < argc) delayMs   = std::stoi(argv[++i]);
        else if (a == "--ttl"    && i + 1 < argc) ttl       = std::stoi(argv[++i]);
        else if (a == "--output" && i + 1 < argc) outputPath = argv[++i];
        else if (a == "--loop")                    loop      = true;
        else if (filepath.empty())                 filepath  = a;
        else { std::cerr << "[错误] 未知参数: " << a << std::endl; return 1; }
    }

    if (filepath.empty()) {
        std::cerr << "[错误] 未指定输入文件\n";
        return 1;
    }

    // 提取
    auto sentences = extractAisSentences(filepath);
    if (sentences.empty()) {
        std::cerr << "[错误] 未提取到任何 AIS 语句\n";
        return 1;
    }

    // 预览
    int previewCount = (std::min)(3, (int)sentences.size());
    std::cout << "[预览] 前 " << previewCount << " 条:\n";
    for (int i = 0; i < previewCount; ++i)
        std::cout << "  [" << i << "] " << sentences[i] << '\n';
    if ((int)sentences.size() > 3)
        std::cout << "  ... 共 " << sentences.size() << " 条\n";

    // socket — 自动识别组播地址 (224.0.0.0/4, 即 224.0.0.0 ~ 239.255.255.255)
    bool isMcast = false;
    auto dotPos = host.find('.');
    if (dotPos != std::string::npos) {
        int firstOctet = std::stoi(host.substr(0, dotPos));
        isMcast = (firstOctet >= 224 && firstOctet <= 239);
    }

    auto sock = isMcast ? createMulticastSocket(host, port, ttl)
                        : createBroadcastSocket(host, port);
    if (sock == INVALID_SOCK) return 1;

    // 输出文件
    std::ofstream outFile;
    if (!outputPath.empty()) {
        outFile.open(outputPath, std::ios::out | std::ios::trunc);
        if (!outFile.is_open()) {
            std::cerr << "[错误] 无法创建输出文件: " << outputPath << std::endl;
        } else {
            std::cout << "[信息] 输出文件: " << outputPath << std::endl;
        }
    }

    // 发送
    std::cout << "[开始] -> " << host << ":" << port
              << "  共 " << sentences.size() << " 条  间隔 " << delayMs
              << "ms" << (loop ? "  [循环]" : "") << '\n'
              << "----------------------------------------\n";

    int ok = 0, fail = 0, round = 0;

    auto sendAll = [&] {
        round++;
        if (loop) std::cout << "\n=== 第 " << round << " 轮 ===\n";
        for (size_t i = 0; i < sentences.size(); ++i) {
            if (broadcastMessage(sock, host, port, sentences[i])) {
                ok++;
                if (outFile.is_open()) {
                    outFile << sentences[i] << '\n';
                    outFile.flush();
                }
                if ((i + 1) % 100 == 0 || i + 1 == sentences.size())
                    std::cout << "[进度] " << (i + 1) << "/"
                              << sentences.size() << '\n';
            } else {
                fail++;
            }
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    };

    if (loop) {
        std::cout << "[提示] 循环模式，按 Ctrl+C 停止\n";
        while (true) sendAll();
    } else {
        sendAll();
    }

    closeSocket(sock);
    std::cout << "\n[完成] 成功: " << ok << "  失败: " << fail << '\n';
    return fail > 0 ? 1 : 0;
}
