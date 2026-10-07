# Black-box regression checks for the headless CLI diagnostics.
#
# Run with: cmake -DBLOCKS_CLI=<path to project_name_headless> -P tests/cli_diagnostics.cmake
#
# Every malformed numeric option must exit nonzero with an error that names the
# option, quotes the supplied value, and states the accepted range instead of
# leaking a standard-library exception name such as "stoull".

if(NOT DEFINED BLOCKS_CLI)
    message(FATAL_ERROR "BLOCKS_CLI (path to project_name_headless) is required")
endif()

function(expect_numeric_error option value range)
    execute_process(
        COMMAND "${BLOCKS_CLI}" "${option}" "${value}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(result EQUAL 0)
        message(FATAL_ERROR
            "${option} ${value}: expected a nonzero exit\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
    set(expected "invalid value for ${option}: '${value}' (expected integer in ${range})")
    string(FIND "${stderr}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "${option} ${value}: stderr missing \"${expected}\"\nstderr: ${stderr}")
    endif()
endfunction()

function(expect_numeric_ok option value)
    execute_process(
        COMMAND "${BLOCKS_CLI}" "${option}" "${value}" --frames 0
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${option} ${value}: expected success, got exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
endfunction()

# Non-numeric tokens are the regression that used to print "error: stoull".
expect_numeric_error(--seed abc 0..65535)
expect_numeric_error(--level abc 0..19)
expect_numeric_error(--height abc 0..5)
expect_numeric_error(--frames abc 0..1000000000)
expect_numeric_error(--envs abc 1..1000000)
expect_numeric_error(--pieces abc 0..1000000)

# Out-of-range, negative, empty, and overflowing values report too.
expect_numeric_error(--seed 65536 0..65535)
expect_numeric_error(--seed "" 0..65535)
expect_numeric_error(--seed 99999999999999999999999999 0..65535)
expect_numeric_error(--level -1 0..19)
expect_numeric_error(--height 6 0..5)
expect_numeric_error(--frames 9999999999 0..1000000000)
expect_numeric_error(--envs 0 1..1000000)
expect_numeric_error(--pieces 1000001 0..1000000)

# Boundary values stay accepted.
expect_numeric_ok(--seed 65535)
expect_numeric_ok(--level 19)
expect_numeric_ok(--height 5)
expect_numeric_ok(--pieces 1000000)
