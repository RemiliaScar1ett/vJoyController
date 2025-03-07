cmake_minimum_required(VERSION 3.10)

# 项目名称
project(vJoyProject)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)

# 设置源代码文件
set(SOURCE_FILES src/main.cpp)

# 创建可执行文件
add_executable(vJoyApp ${SOURCE_FILES})
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")

# 链接头文件路径
target_include_directories(vJoyApp PRIVATE ${CMAKE_SOURCE_DIR}/vJoySDK/inc)

# 链接库文件的路径
target_link_directories(vJoyApp PRIVATE 
    ${CMAKE_SOURCE_DIR}/vJoySDK/lib/x64
)

target_link_libraries(vJoyApp PRIVATE
    vJoyInterface.lib
)

set_target_properties(vJoyApp PROPERTIES
    INSTALL_RPATH "${CMAKE_SOURCE_PATH}/vJoySDK/lib/x64;${CMAKE_SOURCE_PATH}/.Interception/library/x64"
)

# 设置可执行文件的输出目录为项目根目录
set_target_properties(vJoyApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)