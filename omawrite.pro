QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2

CONFIG += c++17 release
TARGET = omawrite
TEMPLATE = app

# The desktop portal (theme and text scale) is only reachable over D-Bus on Linux.
!macx: QT += dbus

macx {
    TARGET = Omawrite
    ICON = macos/Omawrite.icns
    QMAKE_INFO_PLIST = macos/Info.plist
}

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/systemtheme.h \
    src/vault.h \
    src/themes.h \
    src/spellchecker.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp \
    src/vault.cpp \
    src/themes.cpp

RESOURCES += src/resources.qrc

macx {
    OBJECTIVE_SOURCES += src/spellchecker_mac.mm
    QMAKE_OBJECTIVE_CFLAGS += -fobjc-arc
    LIBS += -framework AppKit
} else {
    SOURCES += src/spellchecker_stub.cpp
}
