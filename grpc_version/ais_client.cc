#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "ais.grpc.pb.h"
#include "log_wrapper.h"
#include <log4cplus/loggingmacros.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using ais::AisRequest;
using ais::AisSentence;
using ais::AisService;

int main(int argc, char* argv[]) {
  // 日志系统初始化:配置文件与可执行文件同目录(CMake 已拷贝到 build 目录)
  initLogging("log4cplus.properties");

  std::string server_address("localhost:50051");
  std::string filter;
  int max_count = 0;

  if (argc >= 2) server_address = argv[1];
  if (argc >= 3) filter = argv[2];
  if (argc >= 4) max_count = std::stoi(argv[3]);

  LOG4CPLUS_INFO(clientLogger(), LOG4CPLUS_TEXT("=== gRPC AIS 客户端 ==="));
  LOG4CPLUS_INFO(clientLogger(), LOG4CPLUS_TEXT("服务器: ") << server_address);
  if (!filter.empty())
    LOG4CPLUS_INFO(clientLogger(), LOG4CPLUS_TEXT("过滤: ") << filter);
  if (max_count > 0)
    LOG4CPLUS_INFO_FMT(clientLogger(), "最多: %d 条", max_count);

  // 创建连接和 Stub
  auto channel = grpc::CreateChannel(
      server_address, grpc::InsecureChannelCredentials());
  std::unique_ptr<AisService::Stub> stub = AisService::NewStub(channel);

  // 构造请求
  AisRequest request;
  request.set_filter(filter);
  request.set_max_count(max_count);

  // 发起服务端流调用
  ClientContext context;
  std::unique_ptr<grpc::ClientReader<AisSentence>> reader =
      stub->StreamAisSentences(&context, request);

  // 循环接收:逐条明细用 DEBUG,避免 INFO 级别下刷屏
  AisSentence sentence;
  int count = 0;
  while (reader->Read(&sentence)) {
    LOG4CPLUS_DEBUG_FMT(clientLogger(), "[%d] %s", sentence.index(),
                        sentence.payload().c_str());
    count++;
  }

  Status status = reader->Finish();
  if (!status.ok()) {
    LOG4CPLUS_ERROR_FMT(clientLogger(), "RPC 失败: %s",
                        status.error_message().c_str());
    return 1;
  }

  LOG4CPLUS_INFO_FMT(clientLogger(), "共接收 %d 条 AIS 语句", count);
  return 0;
}
