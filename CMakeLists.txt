cmake_minimum_required(VERSION 3.16)
project(vcap VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -fsanitize=address,undefined")

# ── Dependencies ────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

# libcurl
find_package(CURL REQUIRED)

# Boost (Beast for WebSocket, Asio for async I/O)
find_package(Boost 1.74 REQUIRED COMPONENTS system thread)

# FFmpeg (muxing / demuxing)
pkg_check_modules(AVFORMAT  REQUIRED libavformat)
pkg_check_modules(AVCODEC   REQUIRED libavcodec)
pkg_check_modules(AVUTIL    REQUIRED libavutil)

# OpenSSL (TLS for WSS)
find_package(OpenSSL REQUIRED)

# nlohmann/json (header-only, fetched if not found)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  )
  FetchContent_MakeAvailable(json)
endif()

# ── Optional: X11 Xext screen capture ────────────────────────────────────────────
find_package(X11 Xext QUIET)
if(X11_FOUND AND X11_Xext_FOUND)
  set(HAVE_X11 TRUE)
  add_compile_definitions(VCAP_HAVE_X11)
  message(STATUS "X11 Xext screen capture: enabled")
else()
  message(STATUS "X11 Xext screen capture: disabled (libX11 / libXext not found)")
endif()

# ── Optional: PipeWire ───────────────────────────────────────────────────────
pkg_check_modules(PIPEWIRE libpipewire-0.3)
if(PIPEWIRE_FOUND)
  set(HAVE_PIPEWIRE TRUE)
  add_compile_definitions(VCAP_HAVE_PIPEWIRE)
  message(STATUS "PipeWire screen capture: enabled")
else()
  message(STATUS "PipeWire screen capture: disabled")
endif()

# ── Sources ──────────────────────────────────────────────────────────────────
set(VCAP_SOURCES
  src/main.cpp
  src/Logger.cpp
  src/Config.cpp
  src/Downloader.cpp
  src/ManifestParser.cpp
  src/CdpClient.cpp
  src/YtDlpProbe.cpp
  src/ScreenCapture.cpp
  src/Muxer.cpp
  src/Pipeline.cpp
  src/Utils.cpp
)

add_executable(vcap ${VCAP_SOURCES})

target_include_directories(vcap PRIVATE
  include
  ${AVFORMAT_INCLUDE_DIRS}
  ${AVCODEC_INCLUDE_DIRS}
  ${AVUTIL_INCLUDE_DIRS}
)

target_link_libraries(vcap PRIVATE
  CURL::libcurl
  Boost::system
  Boost::thread
  OpenSSL::SSL
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
  ${AVFORMAT_LIBRARIES}
  ${AVCODEC_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  pthread
)

if(HAVE_X11)
  target_link_libraries(vcap PRIVATE X11 Xext::X11 Xext X11 Xext Xext)
endif()

if(HAVE_PIPEWIRE)
  target_include_directories(vcap PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
  target_link_libraries(vcap PRIVATE ${PIPEWIRE_LIBRARIES})
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS vcap RUNTIME DESTINATION bin)
