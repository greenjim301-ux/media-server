cmake_minimum_required(VERSION 3.10)
project(media_server)

# C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directory for build binary
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../output)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LINUX")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/base)

# Optional: Find and include external libraries
# Uncomment and adjust paths as needed based on your system setup
find_package(PkgConfig REQUIRED)

pkg_search_module(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_search_module(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_search_module(AVUTIL REQUIRED IMPORTED_TARGET libavutil)

# Add source files from src directory
set(MAIN_SOURCES
    src/MediaServer.cpp
    src/MsSha1.cpp
    src/MsDbMgr.cpp
    src/MsDevMgr.cpp
    src/MsFileSource.cpp
    src/MsGbServer.cpp
    src/MsGbServerHandler.cpp
    src/MsGbSource.cpp
    src/MsHttpClient.cpp
    src/MsHttpHandler.cpp
    src/MsHttpServer.cpp
    src/MsHttpStream.cpp
    src/MsOnvifHandler.cpp
    src/MsResManager.cpp
    src/MsSourceFactory.cpp
    src/MsRtspSource.cpp
    src/MsRtspSink.cpp
    src/tinyxml2/tinyxml2.cpp
    src/sqlite/sqlite3.c
    src/osip2/osip_md5c.c
)

# Add source files from src/base directory
set(BASE_SOURCES
    src/base/MsApp.cpp
    src/base/MsCommon.cpp
    src/base/MsConfig.cpp
    src/base/MsEvent.cpp
    src/base/MsHttpMsg.cpp
    src/base/MsInetAddr.cpp
    src/base/MsLog.cpp
    src/base/MsMsg.cpp
    src/base/MsReactor.cpp
    src/base/MsRtspMsg.cpp
    src/base/MsPortAllocator.cpp
    src/base/MsSipMsg.cpp
    src/base/MsSocket.cpp
    src/base/MsTimer.cpp
)

# Create executable
add_executable(media_server ${MAIN_SOURCES} ${BASE_SOURCES})

# Link libraries
target_link_libraries(media_server PRIVATE pthread)

# Optional: Link FFmpeg libraries if available
# Uncomment when FFmpeg is properly installed and configured
target_link_libraries(media_server PRIVATE
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
)
