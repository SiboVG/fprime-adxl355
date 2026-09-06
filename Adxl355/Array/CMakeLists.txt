set(SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/SynchronizedArray.cpp"
)

set(MOD_DEPS
    Adxl355_Device
)

register_fprime_module()
