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

# XJSEmul (PDF JavaScript emulator). Compiled with XPDF; its use in xpdf.cpp is gated by USE_XJS,
# which an app opts into with "XCONFIG += use_xjs" (mirrors use_yara / use_dex).
!contains(XCONFIG, xjsemul) {
    XCONFIG += xjsemul
    include($$PWD/../XJSEmul/xjsemul.pri)
}
contains(XCONFIG, use_xjs) {
    DEFINES += USE_XJS
}

DISTFILES += \
    $$PWD/LICENSE \
    $$PWD/README.md \
    $$PWD/xpdf.cmake
