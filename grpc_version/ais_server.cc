#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "ais.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using ais::AisRequest;
using ais::AisSentence;
using ais::AisService;

// ============================================================
// 提取 AIS 语句（复用原有逻辑）
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
  while (std::getline(file, line)) {
    std::smatch match;
    if (std::regex_search(line, match, pattern)) {
      sentences.push_back(match.str());
    }
  }
  std::cout << "[信息] 从文件提取到 " << sentences.size() << " 条 AIS 语句" << std::endl;
  return sentences;
}

// ============================================================
// gRPC 服务实现
// ============================================================
class AisServiceImpl final : public AisService::Service {
 public:
  explicit AisServiceImpl(const std::vector<std::string>& sentences)
      : sentences_(sentences) {}

  Status StreamAisSentences(ServerContext* context, const AisRequest* request,
                            ServerWriter<AisSentence>* writer) override {
    int max_count = request->max_count();
    int count = 0;

    for (size_t i = 0; i < sentences_.size(); ++i) {
      // 如果设了 max_count 且已达到，提前结束
      if (max_count > 0 && count >= max_count) break;

      // 如果设了 filter，只发包含关键词的语句
      if (!request->filter().empty() &&
          sentences_[i].find(request->filter()) == std::string::npos) {
        continue;
      }

      AisSentence sentence;
      sentence.set_index(static_cast<int>(i));
      sentence.set_payload(sentences_[i]);
      sentence.set_timestamp_ms(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());

      writer->Write(sentence);
      count++;
    }

    std::cout << "[信息] 客户端请求 filter=" << request->filter()
              << " max=" << request->max_count()
              << " → 发送了 " << count << " 条" << std::endl;
    return Status::OK;
  }

 private:
  std::vector<std::string> sentences_;
};

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "用法: " << argv[0] << " <输入文件> [--port 50051]" << std::endl;
    return 1;
  }

  std::string filepath = argv[1];
  std::string server_address("0.0.0.0:50051");

  if (argc >= 3 && std::string(argv[2]) == "--port" && argc >= 4) {
    server_address = std::string("0.0.0.0:") + argv[3];
  }

  // 提取 AIS 语句
  auto sentences = extractAisSentences(filepath);
  if (sentences.empty()) {
    std::cerr << "[错误] 未提取到任何 AIS 语句" << std::endl;
    return 1;
  }

  // 启动 gRPC 服务
  AisServiceImpl service(sentences);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  std::cout << "[就绪] gRPC AIS 服务端监听 " << server_address << std::endl;
  std::cout << "[提示] 按 Ctrl+C 停止" << std::endl;
  server->Wait();
  return 0;
}
