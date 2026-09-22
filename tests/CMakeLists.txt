# ============================================================================
# ModeFlow – Unit Tests Target Configuration
# ============================================================================

# Localized search: Find the Qt6::Test module strictly within the tests subdirectory
find_package(Qt6 REQUIRED COMPONENTS Test)

# Declare the test runner executable using inherited ${TEST_TARGET} variable
qt_add_executable(${TEST_TARGET}
    ModeFlowTests.cpp
)

# Simply link against our inherited Core Object Library variable!
target_link_libraries(${TEST_TARGET}
    PRIVATE
        ${CORE_TARGET}
        Qt6::Test
)

# Add CTest registration
add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})

# Configure runtime test environment for CTest (dynamic Qt bin path resolution)
if(WIN32)
    get_target_property(_qt_core_location Qt6::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)
    get_filename_component(_qt_prefix_dir "${_qt_bin_dir}/.." ABSOLUTE)

    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.22")
        set_property(TEST ${TEST_TARGET} PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_qt_bin_dir}"
            "QT_PLUGIN_PATH=set:${_qt_prefix_dir}/plugins"
        )
    else()
        set_property(TEST ${TEST_TARGET} PROPERTY ENVIRONMENT
            "PATH=${_qt_bin_dir}\\;$ENV{PATH}"
            "QT_PLUGIN_PATH=${_qt_prefix_dir}/plugins"
        )
    endif()
endif()