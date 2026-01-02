cmake_minimum_required(VERSION 3.15)

project(euleraph CXX)
set(CMAKE_CXX_STANDARD 20)

include(cmake/ProjectOptions.cmake)

# 如果没有设置编译类型，则设置默认编译类型为RelWithDebInfo
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG: ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "CMAKE_SHARED_LINKER_FLAGS: ${CMAKE_SHARED_LINKER_FLAGS}")

include_directories("${PROJECT_SOURCE_DIR}/src")

add_executable(euleraph src/main.cpp 
"src/service/http/euleraph_http_handle.cpp" 
"src/importer/importer.cpp"
"src/storage/writer_wiredtiger.cpp"
"src/storage/reader_wiredtiger.cpp"
"src/storage/one_trx_reader_wiredtiger.cpp"
"src/storage/wiredtiger_common.cpp"
"src/algo/algo_Implementation.cpp"
"src/algo/k_hop_neighbors_count.cpp")

find_package(Drogon CONFIG REQUIRED)
find_package(Xlnt CONFIG REQUIRED)
find_package(args CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WIREDTIGER REQUIRED wiredtiger)
find_package(reflectcpp CONFIG REQUIRED)

target_include_directories(euleraph PRIVATE ${WIREDTIGER_INCLUDE_DIRS})
target_link_directories(euleraph PRIVATE ${WIREDTIGER_LIBRARY_DIRS})

target_link_libraries(euleraph PRIVATE 
    Drogon::Drogon 
    ${WIREDTIGER_LIBRARIES} 
    xlnt::xlnt 
    taywee::args 
    spdlog::spdlog
    reflectcpp::reflectcpp)

# 是否启用tcmalloc
if(euleraph_USE_TCMALLOC)
    pkg_check_modules(TCMALLOC REQUIRED IMPORTED_TARGET libtcmalloc)
    target_include_directories(euleraph PRIVATE ${TCMALLOC_INCLUDE_DIRS})
    target_link_directories(euleraph PRIVATE ${TCMALLOC_LIBRARY_DIRS})
    target_link_libraries(euleraph PRIVATE ${TCMALLOC_LIBRARIES})
endif()

# 是否构建测试用例
if(euleraph_BUILD_TESTS)
    add_subdirectory(test)
endif()