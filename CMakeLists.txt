cmake_minimum_required(VERSION 3.21)
project(cyanbeat VERSION 0.1.0 DESCRIPTION "CyanBeat — local music player" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Svg Sql)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-audio-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)

# Everything except main.cpp lives in a static lib so tests can link it.
add_library(cyanbeat_core STATIC
  src/engine/GstEngine.cpp
  src/library/Track.cpp
  src/library/Scanner.cpp
  src/library/Collection.cpp
  src/library/Library.cpp
  src/library/TrackModel.cpp
  src/ui/Theme.cpp
  src/ui/LcdDisplay.cpp
  src/ui/BrowserPanes.cpp
  src/ui/RatingDelegate.cpp
  src/ui/SourceList.cpp
  src/ui/MainWindow.cpp
)
target_include_directories(cyanbeat_core PUBLIC src)
# GLib/GStreamer headers use the identifiers `signals`/`slots`; keep Qt's keywords out of the way.
target_compile_definitions(cyanbeat_core PUBLIC QT_NO_KEYWORDS
  CYANBEAT_VERSION="${PROJECT_VERSION}")
target_link_libraries(cyanbeat_core PUBLIC
  Qt6::Widgets Qt6::Concurrent Qt6::Svg Qt6::Sql PkgConfig::GST PkgConfig::TAGLIB)

# Resources go on the executable: a static lib's rcc object gets dropped by the linker.
qt_add_executable(cyanbeat src/main.cpp data/cyanbeat.qrc)
target_link_libraries(cyanbeat PRIVATE cyanbeat_core)

include(CTest)
if(BUILD_TESTING)
  qt_add_executable(engine_smoke tests/engine_smoke.cpp)
  target_link_libraries(engine_smoke PRIVATE cyanbeat_core)
  add_test(NAME engine_smoke COMMAND engine_smoke)
  # Needs a real audio file: CYANBEAT_TEST_FILE=/path/song.mp3 ctest. Skips without one.
  set_tests_properties(engine_smoke PROPERTIES SKIP_RETURN_CODE 77)
  qt_add_executable(collection_test tests/collection_test.cpp)
  target_link_libraries(collection_test PRIVATE cyanbeat_core)
  add_test(NAME collection_test COMMAND collection_test)
endif()

install(TARGETS cyanbeat RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/cyanbeat.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        RENAME com.johnnygentoo.CyanBeat.desktop)
install(FILES data/icons/cyanbeat.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME com.johnnygentoo.CyanBeat.svg)
