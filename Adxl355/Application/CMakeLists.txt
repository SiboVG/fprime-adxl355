set(SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/SynchronizedApplication.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/SynchronizedApplication.cpp"
)

set(MOD_DEPS
    Adxl355_Types
)

register_fprime_module()

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/SynchronizedApplication.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/SynchronizedApplicationTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/SynchronizedApplicationTester.cpp"
)
set(UT_MOD_DEPS
    Adxl355_Types
    STest
)
set(UT_AUTO_HELPERS ON)
register_fprime_ut()
