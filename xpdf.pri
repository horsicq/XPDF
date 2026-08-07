INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD

HEADERS += \
    $$PWD/xpdf.h \
    $$PWD/xpdf_def.h \
    $$PWD/xpdfcrypt.h

SOURCES += \
    $$PWD/xpdf.cpp \
    $$PWD/xpdfcrypt.cpp

!contains(XCONFIG, xbinary) {
    XCONFIG += xbinary
    include($$PWD/../Formats/xbinary.pri)
}

# PDF JavaScript emulator (xjs*). Built into XPDF only when an app opts in with
# "XCONFIG += use_pdfjsemul" (mirrors use_yara); its use in xpdf.cpp is gated by USE_PDFJSEMUL.
contains(XCONFIG, use_pdfjsemul) {
    DEFINES += USE_PDFJSEMUL

    HEADERS += \
        $$PWD/xjsast.h \
        $$PWD/xjslexer.h \
        $$PWD/xjsparser.h \
        $$PWD/xjsinterpreter.h \
        $$PWD/xjsemul.h

    SOURCES += \
        $$PWD/xjslexer.cpp \
        $$PWD/xjsparser.cpp \
        $$PWD/xjsinterpreter.cpp \
        $$PWD/xjsemul.cpp
}

DISTFILES += \
    $$PWD/LICENSE \
    $$PWD/README.md \
    $$PWD/xpdf.cmake
