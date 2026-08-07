INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD

# The xjs* files are the PDF JavaScript emulator. Compiled with XPDF; its use in xpdf.cpp
# is gated by USE_PDFJSEMUL, which an app opts into with "XCONFIG += use_pdfjsemul"
# (mirrors use_yara / use_dex).
HEADERS += \
    $$PWD/xpdf.h \
    $$PWD/xpdf_def.h \
    $$PWD/xpdfcrypt.h \
    $$PWD/xjsast.h \
    $$PWD/xjslexer.h \
    $$PWD/xjsparser.h \
    $$PWD/xjsinterpreter.h \
    $$PWD/xjsemul.h

SOURCES += \
    $$PWD/xpdf.cpp \
    $$PWD/xpdfcrypt.cpp \
    $$PWD/xjslexer.cpp \
    $$PWD/xjsparser.cpp \
    $$PWD/xjsinterpreter.cpp \
    $$PWD/xjsemul.cpp

!contains(XCONFIG, xbinary) {
    XCONFIG += xbinary
    include($$PWD/../Formats/xbinary.pri)
}

contains(XCONFIG, use_pdfjsemul) {
    DEFINES += USE_PDFJSEMUL
}

DISTFILES += \
    $$PWD/LICENSE \
    $$PWD/README.md \
    $$PWD/xpdf.cmake
