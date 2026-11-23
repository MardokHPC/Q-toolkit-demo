# Runs one binary and asserts its exit code. A test that only greps stdout
# would pass even if the wrapper forgot to return the code, and the exit codes
# are the contract: 0 reconstructed, 1 mismatch, 2 cannot decide.
#
# Usage:
#   cmake -DTOOL=<path> -DEXPECTED=<int> [-DARGS=<arg;arg>]
#         [-DEXPECT_STDOUT=<regex>] [-DEXPECT_STDERR=<regex>] -P run_expect.cmake

if(NOT DEFINED TOOL OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "run_expect: TOOL and EXPECTED are required")
endif()

execute_process(
    COMMAND "${TOOL}" ${ARGS}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE code
)

if(NOT code EQUAL EXPECTED)
    message(FATAL_ERROR
        "exit ${code}, expected ${EXPECTED}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endif()

if(DEFINED EXPECT_STDOUT AND NOT out MATCHES "${EXPECT_STDOUT}")
    message(FATAL_ERROR "stdout does not match '${EXPECT_STDOUT}'\n--- stdout ---\n${out}")
endif()

if(DEFINED EXPECT_STDERR AND NOT err MATCHES "${EXPECT_STDERR}")
    message(FATAL_ERROR "stderr does not match '${EXPECT_STDERR}'\n--- stderr ---\n${err}")
endif()

message(STATUS "exit ${code} as expected")
