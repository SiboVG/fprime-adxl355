cmake_minimum_required(VERSION 3.16)

project(Adxl355HostTests LANGUAGES CXX)

enable_testing()

option(ADXL355_ENABLE_COVERAGE "Instrument ADXL355 host tests for gcov coverage" OFF)

add_library(adxl355_core STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../Adxl355/Device/Device.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../Adxl355/Array/SynchronizedArray.cpp"
)

target_include_directories(adxl355_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}/..")
target_compile_features(adxl355_core PUBLIC cxx_std_11)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(adxl355_core PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_executable(adxl355-unit-tests
    "${CMAKE_CURRENT_LIST_DIR}/CoreTests.cpp"
)
target_link_libraries(adxl355-unit-tests PRIVATE adxl355_core)
target_compile_features(adxl355-unit-tests PRIVATE cxx_std_11)

add_executable(adxl355-integration-tests
    "${CMAKE_CURRENT_LIST_DIR}/ArrayIntegrationTests.cpp"
)
target_link_libraries(adxl355-integration-tests PRIVATE adxl355_core)
target_compile_features(adxl355-integration-tests PRIVATE cxx_std_11)

if(ADXL355_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(adxl355_core PRIVATE --coverage)
        target_link_options(adxl355-unit-tests PRIVATE --coverage)
        target_link_options(adxl355-integration-tests PRIVATE --coverage)
    else()
        message(FATAL_ERROR "ADXL355_ENABLE_COVERAGE requires Clang or GCC")
    endif()
endif()

add_test(NAME adxl355-unit-tests COMMAND adxl355-unit-tests)
add_test(NAME adxl355-integration-tests COMMAND adxl355-integration-tests)
