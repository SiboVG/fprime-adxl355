set(SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/OsDelay.cpp"
)

set(MOD_DEPS
    Adxl355_Device
    Os
)

register_fprime_module()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/OsDelay.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()
