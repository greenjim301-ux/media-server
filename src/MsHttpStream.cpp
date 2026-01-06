#include "MsHttpStream.h"
#include "MsDevMgr.h"
#include "MsLog.h"
#include "MsResManager.h"
#include "MsSourceFactory.h"
#include "nlohmann/json.hpp"
#include <thread>
#include "MsConfig.h"

using json = nlohmann::json;

void MsHttpStream::Run()
{
    MsReactor::Run();

    std::string httpIp = MsConfig::Instance()->GetConfigStr("httpIP");
    int httpPort = MsConfig::Instance()->GetConfigInt("httpStreamPort");

    MsInetAddr bindAddr(AF_INET, httpIp, httpPort);
    shared_ptr<MsSocket> sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);

    if (0 != sock->Bind(bindAddr))
    {
        MS_LOG_ERROR("http bind %s:%d err:%d", httpIp.c_str(), httpPort,
                     MS_LAST_ERROR);
        this->Exit();
        return;
    }

    if (0 != sock->Listen())
    {
        MS_LOG_ERROR("http listen %s:%d err:%d", httpIp.c_str(), httpPort,
                     MS_LAST_ERROR);
        this->Exit();
        return;
    }

    MS_LOG_INFO("http stream listen:%s:%d", httpIp.c_str(), httpPort);

    shared_ptr<MsEventHandler> evtHandler = make_shared<MsHttpAcceptHandler>(
        dynamic_pointer_cast<MsIHttpServer>(shared_from_this()));
    shared_ptr<MsEvent> msEvent =
        make_shared<MsEvent>(sock, MS_FD_ACCEPT, evtHandler);

    this->AddEvent(msEvent);

    std::thread worker(&MsReactor::Wait, shared_from_this());
    worker.detach();
}

class MsHttpSink : public MsMeidaSink, public MsEventHandler
{
public:
    MsHttpSink(const std::string &type, const std::string &streamID, int sinkID,
               std::shared_ptr<MsSocket> sock)
        : MsMeidaSink(type, streamID, sinkID), m_sock(sock) {}

    void HandleRead(shared_ptr<MsEvent> evt) override
    {
        char buf[512];
        int ret = m_sock->Recv(buf, 512);
        if (ret == 0)
        {
            this->ActiveClose();
        }
    }

    void HandleClose(shared_ptr<MsEvent> evt) override
    {
        MS_LOG_ERROR("http sink socket closed, streamID:%s, sinkID:%d",
                     m_streamID.c_str(), m_sinkID);
        this->ActiveClose();
    }

    void HandleWrite(shared_ptr<MsEvent> evt) override
    {
        if (m_error)
            return;
        int fd = m_sock->GetFd();
        int ret = 0;

        while (m_queData.size())
        {
            SData &sd = m_queData.front();
            uint8_t *pBuf = sd.m_buf;

            while (sd.m_len > 0)
            {
                ret = send(fd, pBuf, sd.m_len, 0);
                if (ret >= 0)
                {
                    sd.m_len -= ret;
                    pBuf += ret;
                }
                else if (MS_LAST_ERROR == EAGAIN)
                {
                    if (pBuf != sd.m_buf)
                    {
                        memmove(sd.m_buf, pBuf, sd.m_len);
                    }
                    return;
                }
                else if (MS_LAST_ERROR == EINTR)
                {
                    continue;
                }
                else
                {
                    MS_LOG_INFO("http sink streamID:%s, sinkID:%d err:%d",
                                m_streamID.c_str(), m_sinkID, MS_LAST_ERROR);
                    this->ActiveClose();
                    return;
                }
            }

            delete[] sd.m_buf;
            m_queData.pop();
        }

        // unregist write
        m_evt->SetEvent(MS_FD_READ | MS_FD_CLOSE);
        m_reactor->ModEvent(m_evt);

        MS_LOG_INFO("http sink streamID:%s, sinkID:%d all data sent",
                    m_streamID.c_str(), m_sinkID);
    }

    void OnStreamInfo(AVStream *video, int videoIdx, AVStream *audio,
                      int audioIdx) override
    {
        if (m_error || !video)
            return;
        MsMeidaSink::OnStreamInfo(video, videoIdx, audio, audioIdx);
        int buf_size = 2048;
        int ret;

        auto source = MsResManager::GetInstance().GetMediaSource(m_streamID);
        if (!source)
        {
            MS_LOG_ERROR("source not found for streamID:%s",
                         m_streamID.c_str());
            goto err;
        }

        m_reactor = dynamic_pointer_cast<MsReactor>(source);
        if (!m_reactor)
        {
            MS_LOG_ERROR("reactor not found for streamID:%s",
                         m_streamID.c_str());
            goto err;
        }

        m_sock->SetNonBlock();
        m_evt = std::make_shared<MsEvent>(m_sock, MS_FD_READ | MS_FD_CLOSE, shared_from_this());
        m_reactor->AddEvent(m_evt);

        m_pb = avio_alloc_context(
            static_cast<unsigned char *>(av_malloc(buf_size)), buf_size, 1,
            this, nullptr,
            [](void *opaque, const uint8_t *buf, int buf_size) -> int
            {
                MsHttpSink *sink = static_cast<MsHttpSink *>(opaque);
                return sink->WriteBuffer(buf, buf_size);
            },
            nullptr);

        if (m_type == "flv")
            avformat_alloc_output_context2(&m_fmtCtx, nullptr, "flv", nullptr);
        else if (m_type == "ts")
            avformat_alloc_output_context2(&m_fmtCtx, nullptr, "mpegts", nullptr);
        else
            avformat_alloc_output_context2(&m_fmtCtx, nullptr, m_type.c_str(), nullptr);
        if (!m_fmtCtx || !m_pb)
        {
            MS_LOG_ERROR("Failed to allocate format context or IO context");
            goto err;
        }

        m_fmtCtx->pb = m_pb;
        m_fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

        m_outVideo = avformat_new_stream(m_fmtCtx, NULL);
        ret = avcodec_parameters_copy(m_outVideo->codecpar, m_video->codecpar);
        if (ret < 0)
        {
            MS_LOG_ERROR("Failed to copy codec parameters to output stream");
            goto err;
        }
        m_outVideo->codecpar->codec_tag = 0;

        if (m_audio)
        {
            m_outAudio = avformat_new_stream(m_fmtCtx, NULL);
            ret = avcodec_parameters_copy(m_outAudio->codecpar, m_audio->codecpar);
            if (ret < 0)
            {
                MS_LOG_ERROR("Failed to copy codec parameters to output stream");
                goto err;
            }
            m_outAudio->codecpar->codec_tag = 0;
        }

        ret = avformat_write_header(m_fmtCtx, NULL);
        if (ret < 0)
        {
            MS_LOG_ERROR("Error occurred when writing header to HTTP sink");
            goto err;
        }

        return;

    err:
        m_error = true;
        this->DetachSourceNoLock();
        this->ReleaseResources();
    }

    int WriteBuffer(const uint8_t *buf, int buf_size)
    {
        if (m_error)
            return -1;

        if (m_firstPacket)
        {
            m_firstPacket = false;
            MsHttpMsg rsp;

            rsp.m_version = "HTTP/1.1";
            rsp.m_status = "200";
            rsp.m_reason = "OK";
            rsp.m_connection.SetValue("close");
            if (m_type == "flv")
            {
                rsp.m_contentType.SetValue("video/x-flv");
            }
            else if (m_type == "ts")
            {
                rsp.m_contentType.SetValue("video/MP2T");
            }
            else
            {
                rsp.m_contentType.SetValue("application/octet-stream");
            }

            rsp.m_transport.SetValue("chunked");
            rsp.m_access.SetValue("*");
            rsp.m_access2.SetValue("GET, POST, OPTIONS");
            rsp.m_access3.SetValue(
                "DNT,X-Mx-ReqToken,range,Keep-Alive,User-Agent,X-Requested-With,If-"
                "Modified-Since,Cache-Control,Content-Type,Authorization");

            SendHttpRsp(m_sock.get(), rsp);
        }

        // Format chunk size in hex
        char chunk_header[32];
        int header_len = snprintf(chunk_header, sizeof(chunk_header), "%x\r\n", buf_size);
        int total_chunk_size = header_len + buf_size + 2; // +2 for \r\n after data

        if (m_queData.size())
        {
            SData sd;
            sd.m_buf = new uint8_t[total_chunk_size];
            memcpy(sd.m_buf, chunk_header, header_len);
            memcpy(sd.m_buf + header_len, buf, buf_size);
            memcpy(sd.m_buf + header_len + buf_size, "\r\n", 2);
            sd.m_len = total_chunk_size;
            m_queData.push(sd);
        }
        else
        {
            if (SendSmallBlock((const uint8_t *)chunk_header, header_len, m_sock->GetFd()) < 0)
            {
                MS_LOG_ERROR("http sink send chunk header failed");
                return buf_size;
            }

            int fd = m_sock->GetFd();
            const uint8_t *pBuf = buf;
            int pLen = buf_size;
            int ret = 0;

            while (pLen > 0)
            {
                ret = send(fd, pBuf, pLen, 0);
                if (ret >= 0)
                {
                    pLen -= ret;
                    pBuf += ret;
                }
                else if (MS_LAST_ERROR == EAGAIN)
                {
                    SData sd;
                    sd.m_buf = new uint8_t[pLen + 2];
                    memcpy(sd.m_buf, pBuf, pLen);
                    memcpy(sd.m_buf + pLen, "\r\n", 2);
                    sd.m_len = pLen + 2;
                    m_queData.push(sd);

                    // regist write
                    m_evt->SetEvent(MS_FD_READ | MS_FD_CLOSE | MS_FD_CONNECT);
                    m_reactor->ModEvent(m_evt);

                    MS_LOG_INFO("sink %s:%d regist write", m_type.c_str(),
                                m_sinkID);

                    return buf_size;
                }
                else if (MS_LAST_ERROR == EINTR)
                {
                    continue;
                }
                else
                {
                    MS_LOG_INFO("sink %s:%d err:%d", m_type.c_str(), m_sinkID,
                                MS_LAST_ERROR);
                    return buf_size;
                }
            }

            if (SendSmallBlock((const uint8_t *)"\r\n", 2, m_sock->GetFd()) < 0)
            {
                MS_LOG_ERROR("http sink send chunk tail failed");
                return buf_size;
            }
        }

        return buf_size;
    }

    void ActiveClose()
    {
        m_error = true;

        this->DetachSource();
        this->ReleaseResources();
    }

    void ReleaseResources()
    {
        this->clear_que();

        if (m_evt && m_reactor)
        {
            m_reactor->DelEvent(m_evt);
            m_evt = nullptr;
        }

        if (m_fmtCtx)
        {
            av_write_trailer(m_fmtCtx);
            avformat_free_context(m_fmtCtx);
            m_fmtCtx = nullptr;
        }

        if (m_pb)
        {
            av_free(m_pb->buffer);
            av_free(m_pb);
            m_pb = nullptr;
        }
    }

    void OnSourceClose() override
    {
        m_error = true;
        this->ReleaseResources();
    }

    void OnStreamPacket(AVPacket *pkt) override
    {
        if (!m_fmtCtx || !m_video || m_error)
        {
            return;
        }
        int ret;

        AVStream *outSt = pkt->stream_index == m_videoIdx ? m_outVideo : m_outAudio;
        AVStream *inSt = pkt->stream_index == m_videoIdx ? m_video : m_audio;
        int outIdx = pkt->stream_index == m_videoIdx ? m_outVideoIdx : m_outAudioIdx;
        int inIdx = pkt->stream_index;

        /* copy packet */
        av_packet_rescale_ts(pkt, inSt->time_base, outSt->time_base);
        pkt->pos = -1;
        pkt->stream_index = outIdx;

        ret = av_write_frame(m_fmtCtx, pkt);
        if (ret < 0)
        {
            MS_LOG_ERROR("Error writing frame to HTTP sink, ret:%d", ret);
        }

        av_packet_rescale_ts(pkt, outSt->time_base, inSt->time_base);
        pkt->pos = -1;
        pkt->stream_index = inIdx;
    }

private:
    void clear_que()
    {
        while (m_queData.size())
        {
            SData &sd = m_queData.front();
            delete[] sd.m_buf;
            m_queData.pop();
        }
    }

    std::queue<SData> m_queData;
    std::shared_ptr<MsSocket> m_sock;
    std::shared_ptr<MsReactor> m_reactor;
    std::shared_ptr<MsEvent> m_evt;
    AVFormatContext *m_fmtCtx = nullptr;
    AVIOContext *m_pb = nullptr;
    AVStream *m_outVideo = nullptr;
    AVStream *m_outAudio = nullptr;
    int m_outVideoIdx = 0;
    int m_outAudioIdx = 1;
    bool m_firstPacket = true;
    bool m_error = false;
};

void MsHttpStream::HandleHttpReq(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                                 char *body, int len)
{
    MS_LOG_DEBUG("http stream req uri:%s body:%s", msg.m_uri.c_str(), body);

    std::vector<std::string> s = SplitString(msg.m_uri, "/");
    if (s.size() < 3)
    {
        MS_LOG_WARN("invalid uri:%s", msg.m_uri.c_str());
        json rsp;
        rsp["code"] = 1;
        rsp["msg"] = "invalid uri";
        SendHttpRsp(evt->GetSocket(), rsp.dump());
        return;
    }

    if (s[1] == "live")
    {
        std::vector<std::string> params = SplitString(s[2], ".");
        std::string streamID = params[0];
        std::string format = (params.size() > 1) ? params[1] : "flv";

        if (format != "flv" && format != "ts")
        {
            MS_LOG_WARN("unsupported format:%s", format.c_str());
            json rsp;
            rsp["code"] = 1;
            rsp["msg"] = "unsupported format";
            SendHttpRsp(evt->GetSocket(), rsp.dump());
            return;
        }

        std::shared_ptr<MsMeidaSink> sink = std::make_shared<MsHttpSink>(
            format, streamID, ++m_seqID, evt->GetSharedSocket());

        std::shared_ptr<MsMediaSource> source =
            MsResManager::GetInstance().GetMediaSource(streamID);
        if (source)
        {
            source->AddSink(sink);
        }
        else
        {
            source = MsSourceFactory::CreateMediaSource(streamID);
            if (!source)
            {
                MS_LOG_WARN("create source failed for stream: %s", streamID.c_str());
                json rsp;
                rsp["code"] = 1;
                rsp["msg"] = "create source failed";
                SendHttpRsp(evt->GetSocket(), rsp.dump());
                return;
            }
            source->AddSink(sink);
            source->Work();
        }

        this->DelEvent(evt);
    }
    else if (s[1] == "vod")
    {
        if (s.size() < 5)
        {
            MS_LOG_WARN("invalid vod uri:%s", msg.m_uri.c_str());
            json rsp;
            rsp["code"] = 1;
            rsp["msg"] = "invalid uri";
            SendHttpRsp(evt->GetSocket(), rsp.dump());
            return;
        }

        std::string streamID = s[2];
        std::string format = s[3];
        std::string filename = s[4];

        std::shared_ptr<MsMediaSource> source =
            MsResManager::GetInstance().GetMediaSource(streamID);
        if (source)
        {
            MS_LOG_WARN("vod source already exists for stream: %s",
                        streamID.c_str());
            json rsp;
            rsp["code"] = 1;
            rsp["msg"] = "vod source already exists";
            SendHttpRsp(evt->GetSocket(), rsp.dump());
            return;
        }

        std::shared_ptr<MsMeidaSink> sink = std::make_shared<MsHttpSink>(
            format, streamID, ++m_seqID, evt->GetSharedSocket());

        source = MsSourceFactory::CreateVodSource(streamID, filename);
        if (!source)
        {
            MS_LOG_WARN("create source failed for stream: %s", streamID.c_str());
            json rsp;
            rsp["code"] = 1;
            rsp["msg"] = "create source failed";
            SendHttpRsp(evt->GetSocket(), rsp.dump());
            return;
        }
        source->AddSink(sink);
        source->Work();

        this->DelEvent(evt);
    }
    else
    {
        MS_LOG_WARN("unsupported uri:%s", msg.m_uri.c_str());
        json rsp;
        rsp["code"] = 1;
        rsp["msg"] = "unsupported uri";
        SendHttpRsp(evt->GetSocket(), rsp.dump());
    }
}
