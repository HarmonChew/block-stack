# Black-box check for the live desktop controller protocol.
#
# Run with: cmake -DBLOCKS_APP=<path to block_stack> -P tests/controller_smoke.cmake
#
# The app owns the frame clock and requests one mask per tick. A short mask
# stream drives --controller-smoke through pause, single-frame, restart, and a
# frame-limited game so the test exits without a display or a real controller.
# HOME and XDG_DATA_HOME point at a scratch profile so settings and replays do
# not touch the developer's real app preference directory.

if(NOT DEFINED BLOCKS_APP)
    message(FATAL_ERROR "BLOCKS_APP (path to block_stack) is required")
endif()

if(DEFINED BLOCKS_TEST_HOME)
    set(test_home "${BLOCKS_TEST_HOME}")
else()
    set(test_home "${CMAKE_CURRENT_BINARY_DIR}/controller-test-home")
endif()
file(REMOVE_RECURSE "${test_home}")
file(MAKE_DIRECTORY "${test_home}")
set(app_env "${CMAKE_COMMAND}" -E env
    "HOME=${test_home}" "XDG_DATA_HOME=${test_home}/xdg")

set(mask_file "${CMAKE_CURRENT_BINARY_DIR}/controller_masks.txt")
file(WRITE "${mask_file}" "0\n1\n2\n4\n8\n")
execute_process(
    COMMAND ${app_env} "${BLOCKS_APP}" --controller-stdio --controller-smoke --paused
            --rules classic_ntsc_strict --mode endless --level 18 --seed 42
            --frames 3 --speed 8 --controller-label "CTest AI"
    INPUT_FILE "${mask_file}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 30
)
file(REMOVE "${mask_file}")

if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "controller smoke: expected success, got exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

string(REGEX MATCH "^BLOCK_STACK_CONTROLLER 1\r?\n" banner "${stdout}")
if(NOT banner)
    message(FATAL_ERROR
        "controller smoke: missing protocol banner\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

foreach(message IN ITEMS "BEGIN " "STATE " "ABORT " "END ")
    string(FIND "${stdout}" "${message}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "controller smoke: missing ${message}message\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
endforeach()

# One state is requested for the single-frame check, then three more for the
# restart's frame-limited run. The unused trailing masks must not advance it.
string(REGEX MATCHALL "STATE " states "${stdout}")
list(LENGTH states state_count)
if(NOT state_count EQUAL 4)
    message(FATAL_ERROR
        "controller smoke: expected 4 state requests, got ${state_count}\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

# A reply outside the controller mask range must fail before a tick and name
# both the rejected reply and the accepted range.
set(bad_mask_file "${CMAKE_CURRENT_BINARY_DIR}/controller_bad_mask.txt")
file(WRITE "${bad_mask_file}" "32\n")
execute_process(
    COMMAND ${app_env} "${BLOCKS_APP}" --controller-stdio --controller-smoke --paused
            --rules classic_ntsc_strict --mode endless --level 18 --seed 42
            --frames 3 --speed 8 --controller-label "CTest AI"
    INPUT_FILE "${bad_mask_file}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 30
)
file(REMOVE "${bad_mask_file}")

if(result EQUAL 0)
    message(FATAL_ERROR
        "controller mask 32: expected a nonzero exit\nstdout: ${stdout}\nstderr: ${stderr}")
endif()
set(expected "invalid controller mask: '32' (expected integer in 0..31)")
string(FIND "${stderr}" "${expected}" found)
if(found EQUAL -1)
    message(FATAL_ERROR
        "controller mask 32: stderr missing \"${expected}\"\nstderr: ${stderr}")
endif()

function(expect_desktop_error description expected)
    execute_process(
        COMMAND ${app_env} "${BLOCKS_APP}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 30
    )
    if(result EQUAL 0)
        message(FATAL_ERROR
            "${description}: expected a nonzero exit\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
    string(FIND "${stderr}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "${description}: stderr missing \"${expected}\"\nstderr: ${stderr}")
    endif()
endfunction()

# Controller configuration errors use the same wording as the headless CLI.
expect_desktop_error("bad numeric option"
    "invalid value for --level: 'abc' (expected integer in 0..19)"
    --controller-stdio --level abc)
expect_desktop_error("missing numeric value"
    "missing value for option --seed"
    --controller-stdio --seed)
expect_desktop_error("bad mode"
    "invalid mode: bogus (valid modes: endless, challenge)"
    --controller-stdio --mode bogus)
