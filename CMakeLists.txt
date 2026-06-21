cmake_minimum_required(VERSION 3.16)

# Must be set before project() — CMake reads the toolchain during project(),
# so setting it after is too late to trigger vcpkg manifest install.
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")

project(MarketStreamPulse CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(Boost CONFIG REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/SymbolRegistry.cpp
    src/RawMessagePool.cpp
    src/BinanceWebSocketClient.cpp
    src/BinanceTradeParser.cpp
    src/TradeAggregator.cpp
    src/Metrics.cpp
    src/ConsoleReporter.cpp
)

add_executable(market_stream_pulse ${SOURCES})

target_include_directories(market_stream_pulse PRIVATE src)

target_link_libraries(market_stream_pulse
    PRIVATE
        Boost::system
        Boost::thread
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson::simdjson
        Threads::Threads
)

# Windows: link ws2_32 / mswsock and define minimum target version (Windows 10)
if(WIN32)
    target_compile_definitions(market_stream_pulse PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(market_stream_pulse PRIVATE ws2_32 mswsock crypt32)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(market_stream_pulse PRIVATE /W3 /wd4996)
else()
    target_compile_options(market_stream_pulse PRIVATE -Wall -Wextra -Wpedantic)
endif()
