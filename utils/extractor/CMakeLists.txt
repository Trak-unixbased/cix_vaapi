include_directories(${CMAKE_CURRENT_LIST_DIR})

set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/extractor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/extractor.h
    ${CMAKE_CURRENT_LIST_DIR}/extractor_h264.cpp
    ${CMAKE_CURRENT_LIST_DIR}/extractor_h264.h
    ${CMAKE_CURRENT_LIST_DIR}/extractor_hevc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/extractor_hevc.h
    PARENT_SCOPE
)
