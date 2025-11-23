cmake_minimum_required(VERSION 3.12)
project(secure-tunnel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer vcpkg package names when available
# vcpkg provides MbedTLS as CONFIG package with targets MbedTLS::mbedtls, etc.
find_package(MbedTLS CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include_directories(src)

set(SRC_COMMON
    src/main.cpp
    src/session_manager.cpp
    src/tls_wrapper.cpp
    src/control_protocol.cpp
    src/signal_handler.cpp
    src/utils.cpp
    src/framing.cpp
    src/io_bridge.cpp
)

if (WIN32)
    set(SRC_PLATFORM
        src/listener_win.cpp
        src/pty_handler_win.cpp
        src/resize_coalescer_win.cpp
    )
else()
    set(SRC_PLATFORM
        src/listener.cpp
        src/pty_handler.cpp
        src/resize_coalescer.cpp
    )
endif()

add_executable(secure-tunnel ${SRC_COMMON} ${SRC_PLATFORM})

target_link_libraries(secure-tunnel MbedTLS::mbedtls nlohmann_json::nlohmann_json)
if(UNIX AND NOT WIN32)
    target_link_libraries(secure-tunnel util)
endif()

if (WIN32)
    # Winsock for networking on Windows (used indirectly by MbedTLS net_sockets)
    target_link_libraries(secure-tunnel ws2_32)
endif()

install(TARGETS secure-tunnel DESTINATION bin)
