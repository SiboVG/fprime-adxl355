set(SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/Manager.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/Manager.cpp"
)

set(MOD_DEPS
    Adxl355_Device
    Adxl355_Platform
    Adxl355_Types
    Drv_Ports
    Os
    Svc_Sched
)

register_fprime_module()

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/Manager.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ManagerTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ManagerTester.cpp"
)
set(UT_MOD_DEPS
    Adxl355_Device
    Adxl355_Platform
    Adxl355_Types
    STest
)
set(UT_AUTO_HELPERS ON)
register_fprime_ut()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/Manager.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()
