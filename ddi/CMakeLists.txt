# Include directories
include_directories(${VA_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_LIST_DIR})

# Add the source files
set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/cix_va_ddi.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cix_va_ddi.h
    PARENT_SCOPE
)
