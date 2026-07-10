#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "ais.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using ais::AisRequest;
using ais::AisSentence;
using ais::AisService;

int main(int argc, char* argv[]) {
  std::string server_address("localhost:50051");
  std::string filter;
  int max_count = 0;

  if (argc >= 2) server_address = argv[1];
  if (argc >= 3) filter = argv[2];
  if (argc >= 4) max_count = std::stoi(argv[3]);

  std::cout << "=== gRPC AIS 客户端 ===" << std::endl;
  std::cout << "服务器: " << server_address << std::endl;
  if (!filter.empty()) std::cout << "过滤: " << filter << std::endl;
  if (max_count > 0) std::cout << "最多: " << max_count << " 条" << std::endl;
  std::cout << "------------------------" << std::endl;

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

  // 循环接收
  AisSentence sentence;
  int count = 0;
  while (reader->Read(&sentence)) {
    std::cout << "[" << sentence.index() << "] " << sentence.payload() << std::endl;
    count++;
  }

  Status status = reader->Finish();
  if (!status.ok()) {
    std::cerr << "[错误] RPC 失败: " << status.error_message() << std::endl;
    return 1;
  }

  std::cout << "------------------------" << std::endl;
  std::cout << "[完成] 共接收 " << count << " 条 AIS 语句" << std::endl;
  return 0;
}
