# hodrecomp - extra codegen passes
#
# Codegen loads every [[modules]] DLL of a manifest into one guest address
# space, so DLLs that share an address range can't be recompiled together.
# The bosses take turns in one slot at 0x8BF00000. 'hodtool stage-modules'
# puts the extra ones in passes/pass<N>/: a manifest with its paths rebased to
# the project root, plus a one-line CMakeLists.txt calling
# hodrecomp_add_codegen_pass(<N>).
#
# Each pass builds its modules' shared libraries from the codegen-written
# dll_targets.cmake, the same way the main manifest's are built. It also
# compiles its module_registry.cpp into the executable under a pass-specific
# name. HodRegisterExtraModules(), generated below, calls them all; the app
# runs it at startup.
#
# Codegen also writes passes/pass<N>/generated/rexglue.cmake and a full
# recompile of default.xex into generated/pass<N>/. Both are by-products of
# running codegen on another manifest and aren't built.

if(NOT REXSDK_DIR AND TARGET rex::rexglue)
    get_target_property(HODRECOMP_REXGLUE_EXE rex::rexglue LOCATION)
endif()

# The generated code comes from rexglue's built-in templates, but the codegen
# rules don't depend on the rexglue binary. When it changes (a new SDK revision
# or patches/rexglue-sdk), drop every codegen stamp so all code is regenerated.
if(HODRECOMP_REXGLUE_EXE AND EXISTS "${HODRECOMP_REXGLUE_EXE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HODRECOMP_REXGLUE_EXE}")
    file(SHA256 "${HODRECOMP_REXGLUE_EXE}" rexglue_hash)
    set(rexglue_record "${PROJECT_SOURCE_DIR}/generated/rexglue.sha256")
    set(recorded_hash "")
    if(EXISTS "${rexglue_record}")
        file(READ "${rexglue_record}" recorded_hash)
    endif()
    if(NOT recorded_hash STREQUAL rexglue_hash)
        file(GLOB stamps "${PROJECT_SOURCE_DIR}/generated/*/codegen.build.stamp")
        if(stamps)
            message(STATUS "hodrecomp: rexglue changed; all code will be regenerated")
            file(REMOVE ${stamps})
        endif()
        file(WRITE "${rexglue_record}" "${rexglue_hash}")
    endif()
endif()

# Codegen loads the whole game; run one pass at a time.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS hodrecomp_codegen_pass=1)

# Everything codegen produces, for build scripts that regenerate before
# reconfiguring.
add_custom_target(hodrecomp_codegen_all)
add_dependencies(hodrecomp_codegen_all hodrecomp_codegen)

function(hodrecomp_add_codegen_pass pass)
    set(manifest "${CMAKE_CURRENT_SOURCE_DIR}/hodrecomp_manifest.toml")
    set(out "${PROJECT_SOURCE_DIR}/generated/pass${pass}")

    # First configure after staging: generate now so the targets exist.
    if(NOT EXISTS "${out}/dll_targets.cmake" AND HODRECOMP_REXGLUE_EXE)
        message(STATUS "hodrecomp: running codegen pass ${pass}")
        execute_process(
            COMMAND "${HODRECOMP_REXGLUE_EXE}" codegen "${manifest}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE result)
        if(NOT result EQUAL 0)
            message(WARNING "hodrecomp: codegen pass ${pass} failed (${result})")
        endif()
    endif()
    if(NOT EXISTS "${out}/dll_targets.cmake" OR NOT EXISTS "${out}/module_registry.cpp")
        message(WARNING "hodrecomp: codegen pass ${pass} has no output; its DLLs are left out")
        return()
    endif()

    include("${out}/dll_targets.cmake")
    get_property(module_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(module_sources "")
    foreach(target IN LISTS module_targets)
        get_target_property(sources ${target} SOURCES)
        list(APPEND module_sources ${sources})
    endforeach()

    # Regenerate when an input listed in codegen.d changes, like the main
    # manifest's codegen in generated/rexglue.cmake.
    add_custom_command(
        OUTPUT "${out}/codegen.build.stamp" "${out}/module_registry.cpp" ${module_sources}
        COMMAND $<TARGET_FILE:rex::rexglue> codegen "${manifest}"
        DEPFILE "${out}/codegen.d"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating recompiled code for codegen pass ${pass}"
        JOB_POOL hodrecomp_codegen_pass
        VERBATIM)
    add_custom_target(hodrecomp_codegen_pass${pass} DEPENDS "${out}/codegen.build.stamp")
    add_dependencies(hodrecomp_codegen_all hodrecomp_codegen_pass${pass})
    foreach(target IN LISTS module_targets)
        add_dependencies(${target} hodrecomp_codegen_pass${pass})
    endforeach()

    # The pass's registration function, renamed to sit beside the main one.
    set_source_files_properties("${out}/module_registry.cpp"
        DIRECTORY "${PROJECT_SOURCE_DIR}"
        PROPERTIES
            GENERATED TRUE
            COMPILE_DEFINITIONS
                "RegisterRecompiledModules=hodrecomp_RegisterRecompiledModules_pass${pass}")
    target_sources(${REXGLUE_HOST_TARGET} PRIVATE "${out}/module_registry.cpp")
    add_dependencies(${REXGLUE_HOST_TARGET} hodrecomp_codegen_pass${pass})

    set_property(GLOBAL APPEND PROPERTY HODRECOMP_CODEGEN_PASSES ${pass})
    set_property(GLOBAL APPEND PROPERTY HODRECOMP_MODULE_TARGETS ${module_targets})
endfunction()

# Adds every passes/pass<N> folder and the source defining
# HodRegisterExtraModules(). Call after the host target exists.
function(hodrecomp_add_codegen_passes)
    file(GLOB pass_dirs LIST_DIRECTORIES true "${PROJECT_SOURCE_DIR}/passes/pass*")
    list(SORT pass_dirs COMPARE NATURAL)
    foreach(dir IN LISTS pass_dirs)
        if(EXISTS "${dir}/CMakeLists.txt")
            add_subdirectory("${dir}")
        endif()
    endforeach()

    get_property(passes GLOBAL PROPERTY HODRECOMP_CODEGEN_PASSES)
    set(HODRECOMP_PASS_DECLS "")
    set(HODRECOMP_PASS_CALLS "")
    foreach(pass IN LISTS passes)
        string(APPEND HODRECOMP_PASS_DECLS
            "void hodrecomp_RegisterRecompiledModules_pass${pass}(rex::system::KernelState*);\n")
        string(APPEND HODRECOMP_PASS_CALLS
            "  hodrecomp_RegisterRecompiledModules_pass${pass}(kernel_state);\n")
    endforeach()
    file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/hodrecomp_extra_modules.cpp" CONTENT [=[
// Generated by cmake/codegen_passes.cmake: registers the guest DLLs that were
// recompiled in extra codegen passes (passes/pass<N>).

#include <rex/system/kernel_state.h>

@HODRECOMP_PASS_DECLS@
void HodRegisterExtraModules(rex::system::KernelState* kernel_state) {
  (void)kernel_state;
@HODRECOMP_PASS_CALLS@}
]=] @ONLY)
    target_sources(${REXGLUE_HOST_TARGET} PRIVATE
        "${PROJECT_BINARY_DIR}/hodrecomp_extra_modules.cpp")
endfunction()
