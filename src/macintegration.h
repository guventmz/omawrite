#pragma once

#include <QString>

// Small bits of macOS only reachable through AppKit. Callers guard their use
// with Q_OS_MACOS; the implementation is compiled only there.
namespace MacIntegration {
// Bundle identifier and location of the default web browser.
QString defaultBrowserId();
QString defaultBrowserPath();
// Brings Omawrite back to the front, e.g. after asking a browser to open a window.
void activateApp();
}
