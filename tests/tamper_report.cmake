# Copies the committed report and corrupts every graph node count, leaving the
# rest alone: the verifier must answer 1 (a comparison failed), not 0 and not a
# crash. The substitution is a regex so the fixture cannot go stale when the
# workload grows.
#
# Usage:
#   cmake -DREPORT=<report.json> -DOUT=<tampered report path> -P tamper_report.cmake

if(NOT DEFINED REPORT OR NOT DEFINED OUT)
    message(FATAL_ERROR "tamper_report: REPORT and OUT are required")
endif()

file(READ "${REPORT}" text)
string(REGEX REPLACE "\"nodes\": [0-9]+" "\"nodes\": 999" tampered "${text}")
if(tampered STREQUAL text)
    message(FATAL_ERROR "tamper_report: nothing matched, the report has no \"nodes\": <int>")
endif()

get_filename_component(directory "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${OUT}" "${tampered}")
message(STATUS "tampered copy written to ${OUT}")
