cmake_minimum_required(VERSION 2.20)
project(beep)

if(APPLE)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
elseif(UNIX AND NOT APPLE)
    find_library(ASOUND_LIBRARY asound)
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
