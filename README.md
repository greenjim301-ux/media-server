# Media Server

A  media server implementation supporting GB/T 28181, RTSP, and HTTP streaming protocols.

## Table of Contents
- [Features](#features)
- [Dependencies](#dependencies)
- [Build Instructions](#build-instructions)
- [Configuration](#configuration)
- [Usage](#usage)
- [API Usage](#api-usage)
  - [Get Device List](#get-device-list)
  - [Add RTSP Device](#add-rtsp-device)
  - [Get Device Preview URL](#get-device-preview-url)
  - [GB28181 Integration](#gb28181-integration)
  - [GB28181 Record Playback](#gb28181-record-playback)

## Features

- **GB/T 28181 Support**: Implements GB/T 28181 standard for video surveillance networking systems (Server and Source).
- **RTSP Server**: Supports Real Time Streaming Protocol (RTSP) for media streaming.
- **HTTP Server**: Built-in HTTP server for management and signaling.
- **HTTP Streaming**: Support for media streaming over HTTP.
- **ONVIF Support**: Includes handling for ONVIF protocol.
- **Device Management**: Manages connected devices.
- **Database Integration**: Uses SQLite for data persistence.
- **Extensible Architecture**: Built with a modular design using a reactor pattern.

## Dependencies

The project requires the following dependencies:

- **C++ Compiler**: Supports C++14 standard.
- **CMake**: Version 3.10 or higher.
- **FFmpeg**: Requires `libavcodec`, `libavformat`, and `libavutil`.
- **SQLite3**: Source included.
- **TinyXML2**: Source included.


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

The executable `media_server` will be generated in the `output` directory.

## Configuration

Configuration is loaded from a JSON file. The `conf` directory in the output folder typically contains `config.json`. The server sets up logging, and starts various service modules (GB, RTSP, HTTP) based on this configuration.

**Note:** You need to set \`localBindIP\` in \`config.json\` to the server's IP address before starting the service.

## Usage

Run the compiled executable:

```bash
cd output
./media_server
```

Ensure the configuration file and database are accessible as expected by the application.

## API Usage


### Get Device List

To retrieve the list of devices or a specific device, send a HTTP GET request to `/device`.

**URL:** `http://<server_ip>:<httpPort>/device`

**Method:** `GET`

**Parameters:**

- `deviceId` (optional): The ID of the specific device to retrieve. If omitted, all devices are returned.

**Example using `curl`:**

**Get all devices:**
```bash
curl -X GET http://127.0.0.1:26080/device
```

**Get a specific device:**
```bash
curl -X GET "http://127.0.0.1:26080/device?deviceId=rtsp_cam_01"
```

### Add RTSP Device

To add an RTSP device, send a HTTP POST request to `/device` with the following JSON body:

**URL:** `http://<server_ip>:<httpPort>/device`

**Method:** `POST`

**Body:**

```json
{
    "name": "My_RTSP_Camera",
    "protocol": 2,
    "url": "rtsp://admin:123456@192.168.1.100:554/ch1/main/av_stream"
}
```

- `name`: Display name for the device (string).
- `protocol`: `2` for RTSP device.
- `url`: The full RTSP stream URL.

**Example using `curl`:**

```bash
curl -X POST http://127.0.0.1:26080/device \
  -H "Content-Type: application/json" \
  -d '{
        "name": "Door Camera",
        "protocol": 2,
        "url": "rtsp://192.168.1.50:554/live"
      }'
```

### Get Device Preview URL

To retrieve the live streaming URLs (RTSP, HTTP-TS, HTTP-FLV) for a device.

**URL:** `http://<server_ip>:<httpPort>/device/url`

**Method:** `GET` or `POST`

**Parameters:**

- `deviceId` (required): The ID of the device.
- `netType` (optional): Network type preference.

**Example using `curl`:**

```bash
curl -X POST http://127.0.0.1:26080/device/url \
  -H "Content-Type: application/json" \
  -d '{
        "deviceId": "rtsp_cam_01"
      }'
```

**Response:**

```json
{
    "code": 0,
    "msg": "success",
    "result": {
        "rtspUrl": "rtsp://192.168.1.100:554/live/rtsp_cam_01",
        "httpTsUrl": "http://192.168.1.100:8080/live/rtsp_cam_01.ts",
        "httpFlvUrl": "http://192.168.1.100:8080/live/rtsp_cam_01.flv"
    }
}
```

**Note:** You can use [mpegts.js](https://github.com/xqq/mpegts.js) to play HTTP-FLV/TS streams in the browser.

### GB28181 Integration

1. **Get GB Server Information**

   Retrieve the server information required to configure your GB28181 device or platform.

   **URL:** `http://<server_ip>:<httpPort>/gb/server`
   **Method:** `GET`

   **Example:**
   ```bash
   curl -X GET http://127.0.0.1:26080/gb/server
   ```

   **Response:**
   ```json
   {
       "code": 0,
       "msg": "success",
       "result": {
           "id": "34020000002000000001",
           "ip": "192.168.1.100",
           "port": 5080,
           "pass": "12345678",
           "rtpTransport": 2
       }
   }
   ```

   **Note on `rtpTransport`:**
   The `rtpTransport` field indicates the transport method the GB server uses to receive streams from the device or platform:
   - `0`: UDP
   - `1`: TCP Active
   - `2`: TCP Passive (Default)

   You can change this value by modifying the `rtpTransport` field in `conf/config.json`.

2. **Configure GB Device/Platform**

   Use the information from the previous step (`id`, `ip`, `port`, `pass`) to configure the "SIP Server" or "Platform Access" settings on your GB28181 device or platform.

3. **Verify Registration**

   After configuration, the device should register automatically. Check the registered domains/devices.

   **URL:** `http://<server_ip>:<httpPort>/gb/domain`
   **Method:** `GET`

   **Example:**
   ```bash
   curl -X GET http://127.0.0.1:26080/gb/domain
   ```

4. **Sync Device Channel List**

   The server usually syncs the device list automatically upon registration. If this does not happen, you can trigger it manually.

   **URL:** `http://<server_ip>:<httpPort>/gb/catalog`
   **Method:** `GET`

   **Example:**
   ```bash
   curl -X GET http://127.0.0.1:26080/gb/catalog
   ```

   Once the device channels are synced, you can use the [Get Device List](#get-device-list) and [Get Device Preview URL](#get-device-preview-url) APIs to access the video streams.

### GB28181 Record Playback

1. **Query Record List**

   To query the recording files on a device, send a POST request to `/gb/record`.

   **URL:** `http://<server_ip>:<httpPort>/gb/record`
   **Method:** `POST`

   **Body:**
   ```json
   {
       "deviceId": "34020000001320000001",
       "startTime": "2023-10-27T10:00:00",
       "endTime": "2023-10-27T11:00:00",
       "type": "all"
   }
   ```

   - `deviceId`: GB device ID (must be sub-device/channel ID)
   - `startTime`: Start time (ISO 8601 format or similar string)
   - `endTime`: End time
   - `type`: Record type ("all", etc.)


2. **Get Playback URL**

   After identifying the recording you want to play, request the playback stream URL.

   **URL:** `http://<server_ip>:<httpPort>/gb/record/url`
   **Method:** `POST`

   **Body:**
   ```json
   {
       "deviceId": "34020000001320000001",
       "startTime": "2023-10-27T10:00:00",
       "endTime": "2023-10-27T11:00:00",
       "type": "time"
   }
   ```

   **Response:**
   ```json
   {
       "code": 0,
       "msg": "success",
       "result": {
           "rtspUrl": "rtsp://192.168.1.100:554/gbvod/randStr/34020000001320000001-start-end",
           "httpTsUrl": "http://192.168.1.100:8080/gbvod/randStr/34020000001320000001-start-end.ts",
           "httpFlvUrl": "http://192.168.1.100:8080/gbvod/randStr/34020000001320000001-start-end.flv"
       }
   }
   ```

   You can use RTSP or HTTP-FLV/TS players (like mpegts.js) to play these playback streams.
