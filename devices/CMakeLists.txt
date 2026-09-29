include_directories(${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(v4l2)
add_subdirectory(cme)

set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/device_common.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device_common.h
    ${CMAKE_CURRENT_LIST_DIR}/surface_pool.cpp
    ${CMAKE_CURRENT_LIST_DIR}/surface_pool.h
    PARENT_SCOPE
)
