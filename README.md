# Media Server

A  media server implementation supporting GB/T 28181, RTSP, and HTTP streaming protocols.

## Features

- **GB/T 28181 Support**: Implements GB/T 28181 standard for video surveillance networking systems (Server and Source).
- **RTSP Server**: Supports Real Time Streaming Protocol (RTSP) for media streaming.
- **HTTP Server**: Built-in HTTP server for management and signaling.
- **HTTP Streaming**: Support for media streaming over HTTP.
- **ONVIF Support**: Includes handling for ONVIF protocol.
- **Device Management**: Manages connected devices (`MsDevMgr`).
- **Database Integration**: Uses SQLite for data persistence (`MsDbMgr`).
- **Extensible Architecture**: Built with a modular design using a reactor pattern (`MsReactor`).

## Dependencies

The project requires the following dependencies:

- **C++ Compiler**: Supports C++14 standard.
- **CMake**: Version 3.10 or higher.
- **FFmpeg**: Requires `libavcodec`, `libavformat`, and `libavutil`.
- **SQLite3**: Embedded source included.
- **TinyXML2**: Embedded source included.


## Build Instructions

1. **Create a build directory:**
   ```bash
   mkdir build
   cd build
   ```

2. **Configure with CMake:**
   ```bash
   cmake ..
   ```

3. **Build the project:**
   ```bash
   cmake --build .
   ```

The executable `media_server` will be generated in the `output` directory (parent of `build`).

## Configuration

Configuration is loaded from a JSON file. The `conf` directory in the output folder typically contains `config.json`. The server sets up logging, database connections, and starts various service modules (GB, RTSP, HTTP) based on this configuration.

**Note:** You need to set \`localBindIP\` in \`config.json\` to the server's IP address before starting the service.

## Usage

Run the compiled executable:

```bash
cd output
./media_server
```

Ensure the configuration file and database are accessible as expected by the application.
