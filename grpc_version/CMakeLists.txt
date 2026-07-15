cmake_minimum_required(VERSION 3.16)
project(AisGrpc C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 查找 protobuf 和 gRPC
option(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# 查找 log4cplus(需先编译安装,见计划第二部分)。
# 找不到时用: cmake -B build -S . -Dlog4cplus_DIR=$HOME/log4cplus_install/lib/cmake/log4cplus
find_package(log4cplus CONFIG REQUIRED)

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
set(_GRPC_GRPCPP gRPC::grpc++)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# 编译 proto
get_filename_component(ais_proto "ais.proto" ABSOLUTE)
get_filename_component(ais_proto_path "${ais_proto}" PATH)

set(ais_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/ais.pb.cc")
set(ais_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/ais.pb.h")
set(ais_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/ais.grpc.pb.cc")
set(ais_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/ais.grpc.pb.h")

add_custom_command(
  OUTPUT "${ais_proto_srcs}" "${ais_proto_hdrs}" "${ais_grpc_srcs}" "${ais_grpc_hdrs}"
  COMMAND ${_PROTOBUF_PROTOC}
  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
    --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
    -I "${ais_proto_path}"
    --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
    "${ais_proto}"
  DEPENDS "${ais_proto}"
)

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# proto 库
add_library(ais_proto
  ${ais_grpc_srcs} ${ais_grpc_hdrs}
  ${ais_proto_srcs} ${ais_proto_hdrs})
target_link_libraries(ais_proto
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

# 日志封装库:集中 log4cplus 初始化与命名 logger,供 server/client 共用
add_library(log_wrapper STATIC log_wrapper.cc log_wrapper.h)
target_link_libraries(log_wrapper PUBLIC log4cplus::log4cplus)
target_include_directories(log_wrapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 把日志配置文件拷到构建目录,运行时按可执行文件相对路径读取
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/log4cplus.properties
  ${CMAKE_CURRENT_BINARY_DIR}/log4cplus.properties COPYONLY)

# 服务端
add_executable(ais_server ais_server.cc)
target_link_libraries(ais_server log_wrapper ais_proto ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

# 客户端
add_executable(ais_client ais_client.cc)
target_link_libraries(ais_client log_wrapper ais_proto ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})
