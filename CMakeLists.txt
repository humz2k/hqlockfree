cmake_minimum_required(VERSION 3.30)

project(hqlockfree)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FetchContent)

if (${PROJECT_IS_TOP_LEVEL})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wpedantic")
endif()

file( GLOB_RECURSE SOURCES src/*.cpp )

add_library(hqlockfree ${SOURCES})
target_include_directories(hqlockfree PUBLIC include)

if (${PROJECT_IS_TOP_LEVEL})

file( GLOB EXAMPLE_SOURCES examples/*.cpp )
foreach( sourcefile ${EXAMPLE_SOURCES} )
    get_filename_component( name ${sourcefile} NAME_WE )
    add_executable( ${name} ${sourcefile} )
    target_link_libraries( ${name} hqlockfree )
endforeach( sourcefile ${EXAMPLE_SOURCES} )

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)
enable_testing()
file( GLOB TEST_SOURCES tests/*.cpp )
foreach( sourcefile ${TEST_SOURCES} )
    get_filename_component( name ${sourcefile} NAME_WE )
    add_executable( ${name} ${sourcefile} )
    target_link_libraries( ${name} hqlockfree GTest::gtest_main )
    gtest_discover_tests(${name})
endforeach( sourcefile ${TEST_SOURCES} )

find_package(Boost COMPONENTS system REQUIRED)

set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.4
)
FetchContent_MakeAvailable(benchmark)
file( GLOB PERF_SOURCES perf/*.cpp )
foreach( sourcefile ${PERF_SOURCES} )
    get_filename_component( name ${sourcefile} NAME_WE )
    add_executable( ${name} ${sourcefile} )
    target_link_libraries( ${name} hqlockfree ${Boost_LIBRARIES} benchmark::benchmark benchmark::benchmark_main )
endforeach( sourcefile ${PERF_SOURCES} )

endif()