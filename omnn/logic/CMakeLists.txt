get_filename_component(this_target ${CMAKE_CURRENT_SOURCE_DIR} NAME)

project(${this_target})
file(GLOB src *.cpp *.h)
add_library(${this_target} ${src})
target_link_libraries(${this_target} math)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if (${OPENMIND_BUILD_TESTS})
    add_subdirectory(test)
endif ()
