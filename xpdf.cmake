include_directories(${CMAKE_CURRENT_LIST_DIR})

if (NOT DEFINED XBINARY_SOURCES)
    include(${CMAKE_CURRENT_LIST_DIR}/../Formats/xbinary.cmake)
    set(XPDF_SOURCES ${XPDF_SOURCES} ${XBINARY_SOURCES})
endif()

# XJSEmul (PDF JavaScript emulator). Compiled alongside XPDF; its use in xpdf.cpp is gated by USE_XJS.
if (NOT DEFINED XJSEMUL_SOURCES)
    include(${CMAKE_CURRENT_LIST_DIR}/../XJSEmul/xjsemul.cmake)
endif()
set(XPDF_SOURCES ${XPDF_SOURCES} ${XJSEMUL_SOURCES})

set(XPDF_SOURCES
    ${XPDF_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/xpdf.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xpdf.h
    ${CMAKE_CURRENT_LIST_DIR}/xpdf_def.h
    ${CMAKE_CURRENT_LIST_DIR}/xpdfcrypt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xpdfcrypt.h
)
