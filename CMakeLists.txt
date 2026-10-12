cmake_minimum_required(VERSION 3.20)

# 默认 Qt 安装路径（可通过 -DCMAKE_PREFIX_PATH=... 覆盖）
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "G:/qt/6.12.0/llvm-mingw_64" CACHE PATH "Qt install prefix")
endif()

project(SkillManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network Concurrent Xml Charts)

file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
)

add_executable(SkillManager WIN32 ${SOURCES})

target_include_directories(SkillManager PRIVATE src)

target_compile_definitions(SkillManager PRIVATE SKILL_MANAGER_GUI)

target_link_libraries(SkillManager PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Concurrent
    Qt6::Xml
    Qt6::Charts
)
