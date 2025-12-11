cmake_minimum_required(VERSION 3.15)

project(euleraph CXX)

find_package(proxygen REQUIRED)

set(CMAKE_CXX_STANDARD 20)

add_executable(euleraph src/main.cpp)