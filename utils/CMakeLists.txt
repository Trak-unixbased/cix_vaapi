include_directories(${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(tools)
add_subdirectory(packer)
add_subdirectory(parser)
add_subdirectory(extractor)

set(SOURCES
    ${SOURCES}
    PARENT_SCOPE
)
