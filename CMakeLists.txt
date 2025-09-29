#基础设置部分
cmake_minimum_required(VERSION 3.15)
project(httpccc VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#目标定义构建部分
set(MY_PROJECT_SOURCE
    src/main.cpp
    src/socket.cpp)
add_executable(MyProjectExec ${MY_PROJECT_SOURCE})
target_include_directories(MyProjectExec
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)