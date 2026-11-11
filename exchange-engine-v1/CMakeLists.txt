# CMake的版本
cmake_minimum_required(VERSION 3.16)

# 声明 项目名字，使用的语言 C++
project(exchange_engine LANGUAGES CXX)

# 设置 CPP 标准
set(CMAKE_CXX_STANDARD 17)
# 不满足直接报错，不降级到旧的版本
set(CMAKE_CXX_STANDARD_REQUIRED on)
# 生成 compile_commands.json 记录每个源文件的实际编译命令
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 1. 核心库
# 创建名为 engine_core 的库
add_library(
    engine_core 
    "src/book/map_orderbook.cpp"
    "src/driver/command_runner.cpp"
)
# 还要去哪里 搜索 头文件，-I “include”
target_include_directories(engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(engine_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

# 2. 可执行文件
add_executable(
    exchange_engine  # 目标名字，自己起的
    "src/apps/file_driver.cpp" # 源文件，里面定义了 main()
)
target_link_libraries(exchange_engine PRIVATE engine_core)