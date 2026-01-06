# 媒体服务器 (Media Server)

一个支持 GB/T 28181、RTSP 和 HTTP 流媒体协议的媒体服务器实现。

## 目录
- [功能特性](#功能特性)
- [依赖项](#依赖项)
- [构建指南](#构建指南)
- [配置](#配置)
- [使用方法](#使用方法)
- [API 使用](#api-使用)
  - [获取设备列表](#获取设备列表)
  - [添加 RTSP 设备](#添加-rtsp-设备)
  - [获取设备预览地址](#获取设备预览地址)
  - [GB28181 集成](#gb28181-集成)
  - [GB28181 录像回放](#gb28181-录像回放)

## 功能特性

- **GB/T 28181 支持**: 实现视频监控联网系统的 GB/T 28181 标准（支持服务器端和设备端）。
- **RTSP 服务器**: 支持实时流传输协议 (RTSP) 进行媒体流分发。
- **HTTP 服务器**: 内置 HTTP 服务器，用于管理和信令交互。
- **HTTP 流媒体**: 支持通过 HTTP 协议传输媒体流。
- **ONVIF 支持**: 包含对 ONVIF 协议的处理。
- **设备管理**: 管理连接的设备。
- **数据库集成**: 使用 SQLite 进行数据持久化。
- **可扩展架构**: 采用基于 reactor 模式的模块化设计。

## 依赖项

通过包管理器或源码安装以下依赖：

- **C++ 编译器**: 支持 C++14 标准。
- **CMake**: 版本 3.10 或更高。
- **FFmpeg**: 需要 `libavcodec`, `libavformat`, 和 `libavutil` 库。
- **SQLite3**: 已包含源码。
- **TinyXML2**: 已包含源码。

## 构建指南

1. **创建构建目录:**
   ```bash
   mkdir build
   cd build
   ```

2. **配置 CMake:**
   ```bash
   cmake ..
   ```

3. **编译项目:**
   ```bash
   cmake --build .
   ```

可执行文件 `media_server` 将生成在 `output` 目录中。

## 配置

配置信息从 JSON 文件加载。输出目录中的 `conf` 文件夹通常包含 `config.json`。服务器将根据此配置设置日志记录、并启动各种服务模块（GB, RTSP, HTTP）。

**注意:** 在启动服务之前，您需要在 `config.json` 中将 `localBindIP` 设置为服务器的 IP 地址。

## 使用方法

运行编译后的可执行文件:

```bash
cd output
./media_server
```

请确保配置文件和数据库可被应用程序正确访问。

## API 使用

### 获取设备列表

要获取设备列表或特定设备，请发送 HTTP GET 请求到 `/device`。

**URL:** `http://<server_ip>:<httpPort>/device`

**方法:** `GET`

**参数:**

- `deviceId` (可选): 要获取的特定设备的 ID。如果省略，将返回所有设备。

**使用 `curl` 的示例:**

**获取所有设备:**
```bash
curl -X GET http://127.0.0.1:26080/device
```

**获取特定设备:**
```bash
curl -X GET "http://127.0.0.1:26080/device?deviceId=rtsp_cam_01"
```

### 添加 RTSP 设备

要添加 RTSP 设备，请发送 HTTP POST 请求到 `/device`，并在 Body 中包含以下 JSON 数据：

**URL:** `http://<server_ip>:<httpPort>/device`

**方法:** `POST`

**Body:**

```json
{
    "name": "My_RTSP_Camera",
    "protocol": 2,
    "url": "rtsp://admin:123456@192.168.1.100:554/ch1/main/av_stream"
}
```

- `name`: 设备的显示名称 (字符串)。
- `protocol`: `2` 代表 RTSP 设备。
- `url`: 完整的 RTSP 流地址。

**使用 `curl` 的示例:**

```bash
curl -X POST http://127.0.0.1:26080/device \
  -H "Content-Type: application/json" \
  -d '{
        "name": "Door Camera",
        "protocol": 2,
        "url": "rtsp://192.168.1.50:554/live"
      }'
```

### 获取设备预览地址

获取设备的实时流地址 (RTSP, HTTP-TS, HTTP-FLV)。

**URL:** `http://<server_ip>:<httpPort>/device/url`

**方法:** `GET` 或 `POST`

**参数:**

- `deviceId` (必填): 设备 ID。
- `netType` (可选): 网络类型偏好。

**使用 `curl` 的示例:**

```bash
curl -X POST http://127.0.0.1:26080/device/url \
  -H "Content-Type: application/json" \
  -d '{
        "deviceId": "rtsp_cam_01"
      }'
```

**响应:**

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

**注意:** 您可以使用 [mpegts.js](https://github.com/xqq/mpegts.js) 在浏览器中播放 HTTP-FLV/TS 流。

### GB28181 集成

1. **获取 GB 服务器信息**

   获取配置 GB28181 设备或平台所需的服务器信息。

   **URL:** `http://<server_ip>:<httpPort>/gb/server`
   **方法:** `GET`

   **示例:**
   ```bash
   curl -X GET http://127.0.0.1:26080/gb/server
   ```

   **响应:**
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

   **关于 `rtpTransport` 的说明:**
   `rtpTransport` 字段指示 GB 服务器用于接收设备或平台流的传输方式：
   - `0`: UDP
   - `1`: TCP 主动 (Active)
   - `2`: TCP 被动 (Passive) (默认)

   您可以通过修改 `conf/config.json` 中的 `rtpTransport` 字段来更改此值。

2. **配置 GB 设备/平台**

   使用上一步获取的信息 (`id`, `ip`, `port`, `pass`) 在您的 GB28181 设备或平台上配置 "SIP 服务器" 或 "平台接入" 设置。

3. **验证注册**

   配置完成后，设备应会自动注册。检查已注册的域/设备。

   **URL:** `http://<server_ip>:<httpPort>/gb/domain`
   **方法:** `GET`

   **示例:**
   ```bash
   curl -X GET http://127.0.0.1:26080/gb/domain
   ```

4. **同步设备通道列表**

   服务器通常在注册时自动同步设备列表。如果未同步，您可以手动触发。

   **URL:** `http://<server_ip>:<httpPort>/gb/catalog`
   **方法:** `GET`

   **示例:**
   ```bash
   curl -X GET http://127.0.0.1:26080/gb/catalog
   ```

   设备通道同步后，您可以使用 [获取设备列表](#获取设备列表) 和 [获取设备预览地址](#获取设备预览地址) API 来访问视频流。

### GB28181 录像回放

1. **查询录像列表**

   要查询设备上的录像文件，请发送 POST 请求到 `/gb/record`。

   **URL:** `http://<server_ip>:<httpPort>/gb/record`
   **方法:** `POST`

   **Body:**
   ```json
   {
       "deviceId": "34020000001320000001",
       "startTime": "2023-10-27T10:00:00",
       "endTime": "2023-10-27T11:00:00",
       "type": "all"
   }
   ```

   - `deviceId`: GB 设备 ID (必须是子设备/通道 ID)
   - `startTime`: 开始时间 (ISO 8601 格式或类似字符串)
   - `endTime`: 结束时间
   - `type`: 录像类型 ("all" 等)

  
2. **获取回放地址**

   找到要播放的录像后，请求回放流地址。

   **URL:** `http://<server_ip>:<httpPort>/gb/record/url`
   **方法:** `POST`

   **Body:**
   ```json
   {
       "deviceId": "34020000001320000001",
       "startTime": "2023-10-27T10:00:00",
       "endTime": "2023-10-27T11:00:00",
       "type": "time"
   }
   ```

   **响应:**
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

   您可以使用 RTSP 或 HTTP-FLV/TS 播放器 (如 mpegts.js) 播放这些回放流。
