cmake_minimum_required(VERSION 3.24)

# One owner for the version: the VERSION file. A second literal in
# project(VERSION ...) is the drift this project exists to avoid.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" QTOOLKIT_VERSION LIMIT_COUNT 1)
string(STRIP "${QTOOLKIT_VERSION}" QTOOLKIT_VERSION)

project(qtoolkit_demo
    VERSION ${QTOOLKIT_VERSION}
    DESCRIPTION "Four CUDA libraries composed into one verified artifact"
    HOMEPAGE_URL "https://github.com/MardokHPC/Q-toolkit-demo"
    LANGUAGES CXX
)

include(cmake/pins.cmake)

# Two profiles. OFF is what a runner without a toolkit builds: no
# cuda-timer, no CUDA glue, no nvcc. ON fetches all four and produces the
# artifacts.
option(QTOOLKIT_DEMO_WITH_CUDA "Fetch cuda-timer and build the CUDA capture path" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(QTOOLKIT_DEMO_WITH_CUDA)
    # sm_75 is the reference card (RTX 2060 SUPER). The build script pins nvcc
    # to CUDA 12.6: 12.1 is first on PATH on the reference machine and must not
    # win by accident.
    if(NOT CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 75 CACHE STRING "GPU architecture of the measured card")
    endif()
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    find_package(CUDAToolkit REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Fatal warnings. One generator expression per flag: a multi-word genex such
# as "-Wall -Wextra" reaches the compiler as one malformed option.
# ---------------------------------------------------------------------------
function(qtoolkit_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wall>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wextra>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wpedantic>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Werror>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/W4>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/WX>
    )
endfunction()

# The host half of a .cu keeps the same policy; -Wpedantic stays out because
# nvcc's generated line directives trip it and every .cu would fail.
function(qtoolkit_cuda_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/WX>
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall,-Wextra,-Werror>
        )
    endif()
endfunction()

# Before the dependencies: enabling testing here means their add_test calls
# land in a tree ctest can walk, and CI excludes them by name.
include(CTest)

# ---------------------------------------------------------------------------
# Dependencies: four pins, no vendoring, tests of every dependency off (the
# demo owns the only GoogleTest of the build).
# ---------------------------------------------------------------------------
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(ULPGRAD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ULPGRAD_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ulpgrad
    GIT_REPOSITORY ${QTOOLKIT_REPO_ULPGRAD}
    GIT_TAG ${QTOOLKIT_PIN_ULPGRAD}
    GIT_SHALLOW TRUE
)

set(POOLKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The advisor exists only as a binary: tools/ is the only surface.
set(POOLKIT_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(POOLKIT_WITH_CUDA ${QTOOLKIT_DEMO_WITH_CUDA} CACHE BOOL "" FORCE)
FetchContent_Declare(poolkit
    GIT_REPOSITORY ${QTOOLKIT_REPO_POOLKIT}
    GIT_TAG ${QTOOLKIT_PIN_POOLKIT}
    GIT_SHALLOW TRUE
)

# Defaults to ON upstream: without the override a fetch downloads GoogleTest
# and registers dozens of ctest entries of its own.
set(GRAPHLINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GRAPHLINT_WITH_CUDA ${QTOOLKIT_DEMO_WITH_CUDA} CACHE BOOL "" FORCE)
FetchContent_Declare(graph_lint
    GIT_REPOSITORY ${QTOOLKIT_REPO_GRAPH_LINT}
    GIT_TAG ${QTOOLKIT_PIN_GRAPH_LINT}
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ulpgrad poolkit graph_lint)

if(QTOOLKIT_DEMO_WITH_CUDA)
    # cuda-timer makes the CUDA language mandatory (project(... LANGUAGES CUDA CXX)),
    # so it exists in the ON profile only: the host profile cannot contain it.
    set(CUDA_TIMER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CUDA_TIMER_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(CUDA_TIMER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(cuda_timer
        GIT_REPOSITORY ${QTOOLKIT_REPO_CUDA_TIMER}
        GIT_TAG ${QTOOLKIT_PIN_CUDA_TIMER}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cuda_timer)
endif()

# ---------------------------------------------------------------------------
# Targets. Two executables and no library: a demo has a producer and a
# verifier, not an install tree.
# ---------------------------------------------------------------------------

# C++17 pure and toolkit-free: it is the half of the product a GPU-less runner
# can still run. It links the readers, never a CUDA target.
add_executable(qtoolkit_verify verify/main.cpp)
target_include_directories(qtoolkit_verify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/demo)
target_link_libraries(qtoolkit_verify PRIVATE
    graphlint::graphlint
    poolkit::poolkit
)
# The five checks compare report.pins with this file (check 5): the path is the
# build tree's, so a checkout anywhere still verifies its own pins.
target_compile_definitions(qtoolkit_verify PRIVATE
    QTOOLKIT_PINS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/cmake/pins.cmake"
)
qtoolkit_warnings(qtoolkit_verify)

if(QTOOLKIT_DEMO_WITH_CUDA)
    add_executable(qtoolkit_workload
        demo/main.cu
        demo/workload.cu
        demo/ablation.cu
        demo/graph_stage.cpp
        demo/advise_stage.cpp
    )
    target_link_libraries(qtoolkit_workload PRIVATE
        ulpgrad::ulpgrad
        graphlint::cuda
        cuda_timer::cuda_timer
        poolkit_cuda
        poolkit::poolkit
        CUDA::cudart
    )
    # The pins reach the report from the one file that owns them; the
    # advisor is a binary and its path is the only API it has.
    target_compile_definitions(qtoolkit_workload PRIVATE
        QTOOLKIT_VERSION="${QTOOLKIT_VERSION}"
        QTOOLKIT_PIN_CUDA_TIMER="${QTOOLKIT_PIN_CUDA_TIMER}"
        QTOOLKIT_PIN_POOLKIT="${QTOOLKIT_PIN_POOLKIT}"
        QTOOLKIT_PIN_ULPGRAD="${QTOOLKIT_PIN_ULPGRAD}"
        QTOOLKIT_PIN_GRAPH_LINT="${QTOOLKIT_PIN_GRAPH_LINT}"
        QTOOLKIT_POOLKIT_ADVISE="$<TARGET_FILE:poolkit_advise>"
    )
    qtoolkit_warnings(qtoolkit_workload)
    qtoolkit_cuda_warnings(qtoolkit_workload)
endif()

# ---------------------------------------------------------------------------
# Tests: one GoogleTest in the build (v1.15.2, as the suite), host only.
# Nothing here asserts a duration.
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # GoogleTest is not ours and is not warning-clean at /W4 -Werror: SYSTEM keeps
    # its headers out of the flags that gate this repository's code.
    foreach(qtoolkit_gtest_target IN ITEMS gtest gtest_main)
        if(TARGET ${qtoolkit_gtest_target})
            get_target_property(qtoolkit_gtest_include
                ${qtoolkit_gtest_target} INTERFACE_INCLUDE_DIRECTORIES)
            if(qtoolkit_gtest_include)
                set_target_properties(${qtoolkit_gtest_target} PROPERTIES
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${qtoolkit_gtest_include}")
            endif()
        endif()
    endforeach()

    add_executable(qtoolkit_test_report tests/test_report.cpp)
    target_include_directories(qtoolkit_test_report PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/demo)
    target_link_libraries(qtoolkit_test_report PRIVATE GTest::gtest_main)
    qtoolkit_warnings(qtoolkit_test_report)
    add_test(NAME qtoolkit_test_report COMMAND qtoolkit_test_report)

    # The verifier's exit codes are a contract even when a check cannot run.
    add_test(NAME qtoolkit_verify_usage COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:qtoolkit_verify>
        -DEXPECTED=2
        -DEXPECT_STDERR=usage
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_expect.cmake)
    add_test(NAME qtoolkit_verify_missing_report COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:qtoolkit_verify>
        -DEXPECTED=2
        "-DARGS=${CMAKE_CURRENT_BINARY_DIR}/no-such-report.json"
        -DEXPECT_STDERR=cannot.open
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_expect.cmake)

    # The committed artifacts must reconstruct. This is the job the free CI
    # runner does without a GPU.
    add_test(NAME qtoolkit_report_verified COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:qtoolkit_verify>
        -DEXPECTED=0
        "-DARGS=${CMAKE_CURRENT_SOURCE_DIR}/artifacts/report.json"
        "-DEXPECT_STDOUT=OK   pins"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_expect.cmake)

    # A report that disagrees with its artifacts is exit 1, and the line names
    # the comparison: a verifier that only ever answers 0 would pass this suite.
    add_test(NAME qtoolkit_report_tampered COMMAND ${CMAKE_COMMAND}
        -DREPORT=${CMAKE_CURRENT_SOURCE_DIR}/artifacts/report.json
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/tampered/report.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/tamper_report.cmake)
    add_test(NAME qtoolkit_report_tampered_refused COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:qtoolkit_verify>
        -DEXPECTED=1
        "-DARGS=${CMAKE_CURRENT_BINARY_DIR}/tampered/report.json;${CMAKE_CURRENT_SOURCE_DIR}/artifacts"
        "-DEXPECT_STDOUT=nodes: report 999"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_expect.cmake)
    set_tests_properties(qtoolkit_report_tampered_refused PROPERTIES
        DEPENDS qtoolkit_report_tampered)

    # The table the demo writes must be the table graph-lint accepts, and
    # this runs on a runner with no GPU: it reads two committed files.
    add_test(NAME qtoolkit_weights_accepted COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:graphlint_report>
        -DEXPECTED=0
        "-DARGS=--format;summary;${CMAKE_CURRENT_SOURCE_DIR}/artifacts/graph.json;--weights;${CMAKE_CURRENT_SOURCE_DIR}/artifacts/weights.json"
        "-DEXPECT_STDOUT=ns,"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_expect.cmake)
endif()
