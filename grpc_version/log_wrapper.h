#ifndef LOG_WRAPPER_H
#define LOG_WRAPPER_H

#include <string>
#include <log4cplus/logger.h>

// 初始化日志系统:读取 properties 配置文件。
// propFile 找不到时自动回退到 BasicConfigurator(仅控制台),保证程序仍可运行。
// 必须在 main 开头、任何 LOG4CPLUS_* 宏之前调用。
void initLogging(const std::string& propFile);

// 取命名 logger:服务端用 grpc.server,客户端用 grpc.client。
// 通过命名层次可单独控制某模块的日志级别(见 log4cplus.properties)。
log4cplus::Logger serverLogger();
log4cplus::Logger clientLogger();

#endif  // LOG_WRAPPER_H
