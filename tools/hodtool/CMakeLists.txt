# hodtool - prepares a user's own game files for the recompiler.

# SDK-independent analysis, unit tested without any game data.
add_library(hodtool_core STATIC
    func_scan.cpp
    jmp_scan.cpp
)
target_include_directories(hodtool_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(hodtool_core PUBLIC cxx_std_23)

add_executable(hodtool main.cpp)
target_link_libraries(hodtool PRIVATE hodtool_core hodinstall)
rexglue_apply_target_settings(hodtool)
if(UNIX AND NOT APPLE)
    # Find librexruntime.so next to the binary once installed/copied.
    set_target_properties(hodtool PROPERTIES BUILD_RPATH_USE_ORIGIN ON INSTALL_RPATH "$ORIGIN")
endif()

hodrecomp_stage_windows_dlls(hodtool)
if(WIN32)
    # std::getenv is fine here; MSVC's headers mark it deprecated.
    target_compile_definitions(hodtool PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(BUILD_TESTING)
    add_executable(hodtool_tests tests/hodtool_tests.cpp)
    target_link_libraries(hodtool_tests PRIVATE hodtool_core hodinstall)
    hodrecomp_stage_windows_dlls(hodtool_tests)
    add_test(NAME hodtool_tests
        COMMAND hodtool_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/jmp_scan_fixture.bin)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME hodtool_e2e
            COMMAND ${CMAKE_COMMAND}
                -DPYTHON=${Python3_EXECUTABLE}
                -DHODTOOL=$<TARGET_FILE:hodtool>
                -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/tests
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_test
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_test.cmake)
    endif()
endif()
