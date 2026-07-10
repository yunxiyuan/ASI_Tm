cmake_minimum_required(VERSION 3.16)
project(ASI_Tm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UTF-8 支持 (MSVC)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ========================================
# 可执行文件 1: ASI_transmit (发送端)
# ========================================
add_executable(ASI_transmit
    main.cpp
    AisTransmitter.cpp
    AisTransmitter.h
)

# Windows 需要 ws2_32, Linux 不需要
if(WIN32)
    target_link_libraries(ASI_transmit ws2_32)
endif()

# ========================================
# 可执行文件 2: mcast_receiver (接收端)
# ========================================
add_executable(mcast_receiver
    mcast_receiver.cpp
)

if(WIN32)
    target_link_libraries(mcast_receiver ws2_32)
endif()
