# Native 32-bit Windows toolchain. Deliberately do not set CMAKE_SYSTEM_NAME:
# CMake is running on Windows and wxWidgets' package config then selects gcc_lib.
set(W64DEVKIT_ROOT "D:/tools/w64devkit" CACHE PATH "w64devkit root")
set(ITOOL_WX_ROOT "D:/wxWidgets-3.2.11/w64dev/install" CACHE PATH "wxWidgets install prefix")
unset(WXWIDGETS_ROOT CACHE) # Remove the obsolete variable from older build caches.

set(CMAKE_C_COMPILER "${W64DEVKIT_ROOT}/bin/i686-w64-mingw32-gcc.exe" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${W64DEVKIT_ROOT}/bin/i686-w64-mingw32-g++.exe" CACHE FILEPATH "C++ compiler")
set(CMAKE_RC_COMPILER "${W64DEVKIT_ROOT}/bin/i686-w64-mingw32-windres.exe" CACHE FILEPATH "Resource compiler")

list(PREPEND CMAKE_PREFIX_PATH "${ITOOL_WX_ROOT}")
set(wxWidgets_DIR "${ITOOL_WX_ROOT}/lib/cmake/wxWidgets-3.2" CACHE PATH "wxWidgets package directory" FORCE)
set(wxWidgets_USE_STATIC ON CACHE BOOL "Use static wxWidgets")
