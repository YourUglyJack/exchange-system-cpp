cmake_minimum_required(VERSION 3.16)

project(exchange_engine VERSION 2.0.0 LANGUAGES CXX)

# 使用标准 C++17，禁用编译器特有的语言扩展。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # 生成 compile_commands.json，供 clangd / IDE 使用

# 单配置构建工具未指定构建类型时，默认使用 Debug。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(EXCHANGE_SANITIZE "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(EXCHANGE_BUILD_TESTS "Build unit tests" ON)

add_library(exchange_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(exchange_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# --------------------- Sanitizer -------------------------------------------------------------
# ASan：越界、use-after-free、内存泄漏；UBSan：有符号溢出、空指针解引用等未定义行为。
# 两者对 C++ 新手最有价值的一点：把"有时崩、有时不崩"的 bug 变成"每次都在出错行报错"。
if(EXCHANGE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()


# ------------------------- 核心库 ----------------------------------------------------------------
# 把订单簿做成库，file_driver / fuzz_compare / tests 都链接它，逻辑只有一份。
add_library(exchange_core 
    "src/driver/command_runner.cpp"
    "src/book/map_orderbook.cpp"
    "src/engine/matching_engine.cpp"
)

target_include_directories(exchange_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(exchange_core PRIVATE exchange_warnings)

# -------------------- 可执行程序 --------------------------------
add_executable(file_driver apps/file_driver.cpp)
target_link_libraries(file_driver PRIVATE exchange_core exchange_warnings)

# 对拍入门程序：与文件驱动共用被测引擎，参考引擎独立实现撮合逻辑。
add_executable(fuzz_compare "apps/fuzz_compare.cpp")
target_link_libraries(fuzz_compare PRIVATE exchange_core exchange_warnings)

# ---- 测试 ------------------------------------------------------------------
if(EXCHANGE_BUILD_TESTS)
  enable_testing()

  # 使用本机安装的 GoogleTest，先接入当前已存在的测试文件。
  find_package(GTest CONFIG REQUIRED)

  add_executable(exchange_tests
    "tests/test_order_book.cpp"
    "tests/test_engine_basic.cpp"
    "tests/engine_fixture.hpp"
  )
  
  target_link_libraries(exchange_tests PRIVATE exchange_core exchange_warnings GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(exchange_tests)
endif()
