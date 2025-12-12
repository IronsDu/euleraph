cmake_minimum_required(VERSION 3.15)

project(euleraph CXX)
set(CMAKE_CXX_STANDARD 20)

add_executable(euleraph src/main.cpp "src/service/http/euleraph_http_handle.cpp" "src/importer/importer.cpp")
target_include_directories(euleraph PRIVATE "${PROJECT_SOURCE_DIR}/src")

find_package(Drogon CONFIG REQUIRED)
find_package(Xlnt CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WIREDTIGER REQUIRED wiredtiger)

target_include_directories(euleraph PRIVATE ${WIREDTIGER_INCLUDE_DIRS})
target_link_directories(euleraph PRIVATE ${WIREDTIGER_LIBRARY_DIRS})
target_link_libraries(euleraph PRIVATE Drogon::Drogon ${WIREDTIGER_LIBRARIES} xlnt::xlnt)