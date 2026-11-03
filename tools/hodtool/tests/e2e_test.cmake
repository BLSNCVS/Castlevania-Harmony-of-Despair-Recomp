# End-to-end test for hodtool (info, extract, install, install-dlc on files and
# folders, scan, stage-modules) on synthetic STFS/XEX files. Invoked by CTest with
# -DPYTHON=... -DHODTOOL=... -DSRC=... -DWORK=...

set(HOD_TITLE 58410A7A)

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run_or_fail out_var)
    execute_process(COMMAND ${ARGN}
        WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "command failed (${rc}): ${ARGN}\n${out}\n${err}")
    endif()
    set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

function(expect_text haystack)
    foreach(needle ${ARGN})
        string(FIND "${haystack}" "${needle}" pos)
        if(pos EQUAL -1)
            message(FATAL_ERROR "output lacks '${needle}':\n${haystack}")
        endif()
    endforeach()
endfunction()

function(expect_same_file want got)
    if(NOT EXISTS "${got}")
        message(FATAL_ERROR "missing ${got}")
    endif()
    file(SHA256 "${want}" want_hash)
    file(SHA256 "${got}" got_hash)
    if(NOT want_hash STREQUAL got_hash)
        message(FATAL_ERROR "${got} differs from ${want}")
    endif()
endfunction()

# --- info / extract ----------------------------------------------------------
run_or_fail(_ "${PYTHON}" -I "${SRC}/make_test_stfs.py" "${WORK}/package" "${WORK}/expected")
run_or_fail(info_out "${HODTOOL}" info "${WORK}/package")
expect_text("${info_out}" "title ID     : 0000CAFE" "content type : 000D0000"
            "display name : HoD Test Package" "contents     : 4 files")

run_or_fail(extract_out "${HODTOOL}" extract "${WORK}/package" "${WORK}/out")
foreach(rel default.xex data/stage00.bin data/empty.bin readme.txt)
    expect_same_file("${WORK}/expected/${rel}" "${WORK}/out/${rel}")
endforeach()
file(SHA256 "${WORK}/expected/default.xex" xex_hash)
expect_text("${extract_out}" "default.xex sha256: ${xex_hash}")

# --- install: game + title update + DLC found by searching a folder ----------
file(MAKE_DIRECTORY "${WORK}/console/000D0000" "${WORK}/console/000B0000"
                    "${WORK}/console/00000002")
run_or_fail(_ "${PYTHON}" -I "${SRC}/make_test_stfs.py" "${WORK}/console/000D0000/GAMEPKG"
            "${WORK}/game_expected" --kind game --title ${HOD_TITLE})
run_or_fail(_ "${PYTHON}" -I "${SRC}/make_test_stfs.py" "${WORK}/console/000B0000/TUPKG"
            "${WORK}/tu_expected" --kind tu --title ${HOD_TITLE})
run_or_fail(_ "${PYTHON}" -I "${SRC}/make_test_stfs.py" "${WORK}/console/00000002/HODTESTDLC"
            "${WORK}/dlc_expected" --kind dlc --title ${HOD_TITLE})
file(WRITE "${WORK}/console/notes.txt" "not a package")

run_or_fail(install_out "${HODTOOL}" install "${WORK}/console"
            --game-dir "${WORK}/inst/game" --user-data "${WORK}/inst/user")
expect_text("${install_out}" "Game:          HoD Test Package"
            "Title update:  HoD Test Title Update" "DLC:           HoD Test DLC"
            "Title update  HoD Test Title Update" "DLC           1 pack(s)")
expect_same_file("${WORK}/game_expected/default.xex" "${WORK}/inst/game/default.xex")
expect_same_file("${WORK}/game_expected/data/stage00.bin" "${WORK}/inst/game/data/stage00.bin")
# The title update is overlaid: new files added, shared files replaced.
expect_same_file("${WORK}/tu_expected/default.xexp" "${WORK}/inst/game/default.xexp")
expect_same_file("${WORK}/tu_expected/readme.txt" "${WORK}/inst/game/readme.txt")
# ...and kept whole beside the game for the UPDATE: mount.
expect_same_file("${WORK}/tu_expected/readme.txt" "${WORK}/inst/update/readme.txt")
expect_same_file("${WORK}/tu_expected/default.xexp" "${WORK}/inst/update/default.xexp")
set(dlc_root "${WORK}/inst/user/0000000000000000/${HOD_TITLE}/00000002/HODTESTDLC")
foreach(rel default.xex data/stage00.bin data/empty.bin readme.txt)
    expect_same_file("${WORK}/dlc_expected/${rel}" "${dlc_root}/${rel}")
endforeach()
set(dlc_header "${WORK}/inst/user/0000000000000000/${HOD_TITLE}/Headers/00000002/HODTESTDLC.header")
if(NOT EXISTS "${dlc_header}")
    message(FATAL_ERROR "DLC content header was not written")
endif()
file(SIZE "${dlc_header}" header_size)
if(NOT header_size EQUAL 328)  # sizeof(XCONTENT_AGGREGATE_DATA); no license bits set
    message(FATAL_ERROR "unexpected DLC header size ${header_size}")
endif()

# An extracted game folder works as a source too, and --no-dlc skips DLC.
run_or_fail(_ "${HODTOOL}" install "${WORK}/inst/game" "${WORK}/console/000B0000/TUPKG"
            --game-dir "${WORK}/inst2/game" --user-data "${WORK}/inst2/user" --no-dlc)
expect_same_file("${WORK}/game_expected/default.xex" "${WORK}/inst2/game/default.xex")
if(EXISTS "${WORK}/inst2/user")
    message(FATAL_ERROR "--no-dlc still installed DLC")
endif()

# --- install-dlc on its own -------------------------------------------------
run_or_fail(_ "${HODTOOL}" install-dlc "${WORK}/console/00000002/HODTESTDLC"
            --user-data "${WORK}/user")
expect_same_file("${WORK}/dlc_expected/data/stage00.bin"
    "${WORK}/user/0000000000000000/${HOD_TITLE}/00000002/HODTESTDLC/data/stage00.bin")

# --- install-dlc on a folder: finds the DLC, skips the game and TU ----------
run_or_fail(dlc_folder_out "${HODTOOL}" install-dlc "${WORK}/console"
            --user-data "${WORK}/user_folder")
expect_same_file("${WORK}/dlc_expected/data/stage00.bin"
    "${WORK}/user_folder/0000000000000000/${HOD_TITLE}/00000002/HODTESTDLC/data/stage00.bin")
file(GLOB installed_content RELATIVE
    "${WORK}/user_folder/0000000000000000/${HOD_TITLE}/00000002"
    "${WORK}/user_folder/0000000000000000/${HOD_TITLE}/00000002/*")
if(NOT installed_content STREQUAL "HODTESTDLC")
    message(FATAL_ERROR "install-dlc on a folder installed more than the DLC: ${installed_content}")
endif()

# --- scan through the SDK's real XEX loader ----------------------------------
file(MAKE_DIRECTORY "${WORK}/game")
run_or_fail(_ "${PYTHON}" -I "${SRC}/make_test_xex.py" "${SRC}/jmp_scan_fixture.bin"
            "${WORK}/game/default.xex")
run_or_fail(_ "${HODTOOL}" scan "${WORK}/game" --write "${WORK}/jmp.toml")
file(READ "${WORK}/jmp.toml" jmp_toml)
expect_text("${jmp_toml}" "setjmp_address = 0x8200105C" "longjmp_address = 0x82001150")

# --- install from a folder holding an extracted game in a subfolder ---------
file(MAKE_DIRECTORY "${WORK}/nested/HOD")
file(COPY_FILE "${WORK}/game/default.xex" "${WORK}/nested/HOD/default.xex")
file(WRITE "${WORK}/nested/README.txt" "not a package")
run_or_fail(nested_out "${HODTOOL}" install "${WORK}/nested" --game-dir "${WORK}/nested_install"
            --user-data "${WORK}/nested_user")
expect_text("${nested_out}" "Game:          extracted folder")
expect_same_file("${WORK}/nested/HOD/default.xex" "${WORK}/nested_install/default.xex")

# --- stage-modules: DLLs at the same address go to an extra codegen pass ----
# Copies of the test XEX all load at 0x82000000, like the bosses sharing a slot.
file(MAKE_DIRECTORY "${WORK}/game/dll" "${WORK}/stage")
file(COPY_FILE "${WORK}/game/default.xex" "${WORK}/game/dll/a.dll")
file(COPY_FILE "${WORK}/game/default.xex" "${WORK}/game/dll/b.dll")
file(WRITE "${WORK}/stage/hodrecomp_manifest.toml" [=[
[project]
name = "hodrecomp"
sdk_version = "0.10.0"
game_root = "modules"

[entrypoint]
file_path = "modules/default.xex"
out_directory_path = "generated/default"
includes = ["config/jmp.toml"]
]=])
set(stage_args stage-modules --game "${WORK}/game" --user-data "${WORK}/no_user"
    --out "${WORK}/stage/modules" --manifest "${WORK}/stage/hodrecomp_manifest.toml")
run_or_fail(stage_out "${HODTOOL}" ${stage_args})
expect_text("${stage_out}" "2 DLL module(s)" "2 codegen pass(es)")
file(READ "${WORK}/stage/hodrecomp_manifest.toml" main_manifest)
expect_text("${main_manifest}" "guest_path = \"dll/a.dll\"" "file_path = \"modules/dll/a.dll\"")
if(main_manifest MATCHES "dll/b.dll")
    message(FATAL_ERROR "b.dll overlaps a.dll but is in the main manifest")
endif()
file(READ "${WORK}/stage/passes/pass1/hodrecomp_manifest.toml" pass_manifest)
expect_text("${pass_manifest}"
    "game_root = \"../../modules\""
    "file_path = \"../../modules/default.xex\""
    "out_directory_path = \"../../generated/pass1\""
    "\"../../config/jmp.toml\""
    "guest_path = \"dll/b.dll\""
    "file_path = \"../../modules/dll/b.dll\""
    "out_directory_path = \"../../generated/b\"")
file(READ "${WORK}/stage/passes/pass1/CMakeLists.txt" pass_cmake)
expect_text("${pass_cmake}" "hodrecomp_add_codegen_pass(1)")
expect_same_file("${WORK}/game/dll/b.dll" "${WORK}/stage/modules/dll/b.dll")

# Without the overlap the extra pass goes away again.
file(REMOVE "${WORK}/game/dll/b.dll")
run_or_fail(stage_out "${HODTOOL}" ${stage_args})
expect_text("${stage_out}" "1 codegen pass(es)")
if(EXISTS "${WORK}/stage/passes/pass1")
    message(FATAL_ERROR "stale passes/pass1 left behind")
endif()

message(STATUS "hodtool end-to-end OK")
