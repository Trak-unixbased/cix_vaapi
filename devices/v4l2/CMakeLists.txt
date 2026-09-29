# Include directories
include_directories(${CMAKE_CURRENT_LIST_DIR})

# Add the source files
set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful_device.cpp
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful_device.h
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful.cpp
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful.h
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful_dec.cpp
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful_dec.h
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful_enc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/v4l2_stateful_enc.h
    PARENT_SCOPE
)
