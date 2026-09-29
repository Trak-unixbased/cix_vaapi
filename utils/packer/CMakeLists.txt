include_directories(${CMAKE_CURRENT_LIST_DIR})

set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/packer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/packer.h
    ${CMAKE_CURRENT_LIST_DIR}/packer_h264.cpp
    ${CMAKE_CURRENT_LIST_DIR}/packer_h264.h
    ${CMAKE_CURRENT_LIST_DIR}/packer_hevc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/packer_hevc.h
    ${CMAKE_CURRENT_LIST_DIR}/packer_vp9.cpp
    ${CMAKE_CURRENT_LIST_DIR}/packer_vp9.h
    ${CMAKE_CURRENT_LIST_DIR}/packer_av1.cpp
    ${CMAKE_CURRENT_LIST_DIR}/packer_av1.h
    ${CMAKE_CURRENT_LIST_DIR}/packer_bsw.h
    PARENT_SCOPE
)
