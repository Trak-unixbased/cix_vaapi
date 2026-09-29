include_directories(${CMAKE_CURRENT_LIST_DIR})

set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/log.cpp
    ${CMAKE_CURRENT_LIST_DIR}/log.h
    ${CMAKE_CURRENT_LIST_DIR}/dump.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dump.h
    PARENT_SCOPE
)
