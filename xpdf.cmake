include_directories(${CMAKE_CURRENT_LIST_DIR})

if (NOT DEFINED XBINARY_SOURCES)
    include(${CMAKE_CURRENT_LIST_DIR}/../Formats/xbinary.cmake)
    set(XPDF_SOURCES ${XPDF_SOURCES} ${XBINARY_SOURCES})
endif()

set(XPDF_SOURCES
    ${XPDF_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/xpdf.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xpdf.h
    ${CMAKE_CURRENT_LIST_DIR}/xpdf_def.h
    ${CMAKE_CURRENT_LIST_DIR}/xpdfcrypt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xpdfcrypt.h
    # PDF JavaScript emulator. Compiled with XPDF; its use in xpdf.cpp is gated by USE_PDFJSEMUL.
    ${CMAKE_CURRENT_LIST_DIR}/xjsast.h
    ${CMAKE_CURRENT_LIST_DIR}/xjslexer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xjslexer.h
    ${CMAKE_CURRENT_LIST_DIR}/xjsparser.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xjsparser.h
    ${CMAKE_CURRENT_LIST_DIR}/xjsinterpreter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xjsinterpreter.h
    ${CMAKE_CURRENT_LIST_DIR}/xjsemul.cpp
    ${CMAKE_CURRENT_LIST_DIR}/xjsemul.h
)
