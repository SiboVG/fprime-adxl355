set(SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/Device.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/AccessGate.cpp"
)

# The portable core's only F Prime touchpoint: internal-impossibility
# assertions route to FW_ASSERT so the deployment defines the response.
set(MOD_DEPS
    Fw_Types
)

register_fprime_module()

target_compile_definitions(Adxl355_Device PRIVATE ADXL355_USE_FW_ASSERT)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/Device.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/AccessGate.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()
