cmake_minimum_required(VERSION 3.6)
project(beep)

if(APPLE)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
elseif(UNIX AND NOT APPLE)
    find_library(ASOUND_LIBRARY asound)

    # Get the directory path of the found library
    get_filename_component(ASOUND_LIBRARY_DIR ${ASOUND_LIBRARY} DIRECTORY)

    message("ASOUND_LIBRARY_DIR: ${ASOUND_LIBRARY_DIR}")
    include_directories(${ASOUND_LIBRARY_DIR}/../include)
else()
    # Windows
endif()

## Sources
set(BEEP_SRCS
    beep.c
)

## Add serial library
add_library(${PROJECT_NAME} ${BEEP_SRCS})
if(APPLE)
    target_link_libraries(${PROJECT_NAME} ${AUDIOUNIT_LIBRARY})
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME} ${ASOUND_LIBRARY})
else()
    # Windows
endif()

## Include headers
include_directories(include)
