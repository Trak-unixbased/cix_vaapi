include_directories(${CMAKE_CURRENT_LIST_DIR})

# cme_device.cpp is always built: without libcme it only reports that video
# processing is unavailable. cme_context.cpp calls into libcme.
set(SOURCES
    ${SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/cme_device.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cme_device.h
)

if(CIX_VAAPI_HAVE_CME)
    set(SOURCES
        ${SOURCES}
        ${CMAKE_CURRENT_LIST_DIR}/cme_context.cpp
        ${CMAKE_CURRENT_LIST_DIR}/cme_context.h
    )
endif()

set(SOURCES ${SOURCES} PARENT_SCOPE)
