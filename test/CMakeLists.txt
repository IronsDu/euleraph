include(CTest)
enable_testing()

find_package(GTest REQUIRED)

add_executable(
    wiredtiger_impl_unit_test
    test_wiredtiger_impl.cpp
    "${PROJECT_SOURCE_DIR}/src/storage/writer_wiredtiger.cpp"
    "${PROJECT_SOURCE_DIR}/src/storage/reader_wiredtiger.cpp"
    "${PROJECT_SOURCE_DIR}/src/storage/one_trx_reader_wiredtiger.cpp"
    "${PROJECT_SOURCE_DIR}/src/storage/wiredtiger_common.cpp"
)

target_link_libraries(
    wiredtiger_impl_unit_test
    PRIVATE
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

target_include_directories(wiredtiger_impl_unit_test PRIVATE ${WIREDTIGER_INCLUDE_DIRS})
target_link_directories(wiredtiger_impl_unit_test PRIVATE ${WIREDTIGER_LIBRARY_DIRS})
target_link_libraries(wiredtiger_impl_unit_test PRIVATE Drogon::Drogon ${WIREDTIGER_LIBRARIES} xlnt::xlnt spdlog::spdlog)

add_test(
    NAME wiredtiger_impl_unit_test
    COMMAND wiredtiger_impl_unit_test
)