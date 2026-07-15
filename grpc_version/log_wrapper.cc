#include "log_wrapper.h"

#include <fstream>
#include <log4cplus/initializer.h>
#include <log4cplus/configurator.h>
#include <log4cplus/loggingmacros.h>

// 全局 Initializer:必须早于任何 Logger::getInstance 构造、晚于所有 logger 析构。
// 放在文件级 static,生命周期覆盖整个 main。绝不能放进 initLogging 的局部作用域,
// 否则函数返回后立即析构,后续 LOG4CPLUS_* 宏会崩溃。
static log4cplus::Initializer g_initializer;

void initLogging(const std::string& propFile) {
  std::ifstream f(propFile);
  if (f.good()) {
    // 用 properties 配置:appender / layout / 级别都在文件里,运行时可改。
    log4cplus::PropertyConfigurator::doConfigure(LOG4CPLUS_TEXT(propFile));
  } else {
    // 配置文件找不到时回退到最简配置(仅控制台),保证程序仍能跑起来。
    // doConfigure 默认参数即 getDefaultHierarchy(),无需传参。
    log4cplus::BasicConfigurator::doConfigure();
    LOG4CPLUS_WARN(log4cplus::Logger::getRoot(),
                   LOG4CPLUS_TEXT("找不到日志配置文件: ")
                       << propFile << LOG4CPLUS_TEXT(",使用默认控制台配置"));
  }
}

log4cplus::Logger serverLogger() {
  return log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("grpc.server"));
}

log4cplus::Logger clientLogger() {
  return log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("grpc.client"));
}
