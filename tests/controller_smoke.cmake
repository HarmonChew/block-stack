# Black-box check for the live desktop controller protocol.
#
# Run with: cmake -DBLOCKS_APP=<path to project_name> -P tests/controller_smoke.cmake
#
# The app owns the frame clock and requests one mask per tick. A short mask
# stream drives --controller-smoke through pause, single-frame, restart, and a
# frame-limited game so the test exits without a display or a real controller.

if(NOT DEFINED BLOCKS_APP)
    message(FATAL_ERROR "BLOCKS_APP (path to project_name) is required")
endif()

set(mask_file "${CMAKE_CURRENT_BINARY_DIR}/controller_masks.txt")
file(WRITE "${mask_file}" "0\n1\n2\n4\n8\n")
execute_process(
    COMMAND "${BLOCKS_APP}" --controller-stdio --controller-smoke --paused
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
