include_directories(${CMAKE_CURRENT_LIST_DIR})

set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/cme_context.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cme_context.h
    ${CMAKE_CURRENT_LIST_DIR}/cme_device.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cme_device.h
    PARENT_SCOPE
)
