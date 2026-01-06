#include "MsGbSource.h"
#include <thread>
#include "MsLog.h"
#include "MsConfig.h"
#include "MsPortAllocator.h"

extern "C"
{
#include <libavformat/avformat.h>
}

class MsGbRtpHandler : public MsEventHandler
{
public:
    MsGbRtpHandler(shared_ptr<MsGbSource> source)
        : m_bufSize(64 * 1024), m_bufOff(0), m_source(source)
    {
        m_buf = new char[m_bufSize];
    }

    ~MsGbRtpHandler()
    {
        MS_LOG_INFO("~MsGbRtpHandler");
        delete[] m_buf;
    }

    void HandleRead(shared_ptr<MsEvent> evt) override
    {
        MsSocket *s = evt->GetSocket();
        int n = s->Recv(m_buf + m_bufOff, m_bufSize - m_bufOff);

        if (n <= 0)
        {
            return;
        }

        m_bufOff += n;

        if (m_isTcp == -1)
        {
            m_isTcp = s->IsTcp() ? 1 : 0;
        }

        if (m_isTcp)
        {
            uint8_t *xbuf = (uint8_t *)m_buf;

            while (m_bufOff > 2)
            {
                int pktLen = AV_RB16(xbuf);

                if (pktLen <= 0)
                {
                    MS_LOG_ERROR("pkt len:%d error", pktLen);
                    m_bufOff = 0;
                    return;
                }

                if (pktLen > m_bufOff - 2) // not complete
                {
                    if (m_bufOff)
                    {
                        if (xbuf - (uint8_t *)m_buf != 0)
                        {
                            memmove(m_buf, xbuf, m_bufOff);
                        }
                    }

                    return;
                }
                else
                {
                    m_source->ProcessRtp(xbuf + 2, pktLen);
                    xbuf += pktLen + 2;
                    m_bufOff -= pktLen + 2;
                }
            }

            if (m_bufOff)
            {
                memmove(m_buf, xbuf, m_bufOff);
            }
        }
        else // udp
        {
            m_source->ProcessRtp((uint8_t *)m_buf, m_bufOff);
            m_bufOff = 0;
        }

        if (m_bufOff >= m_bufSize)
        {
            MS_LOG_ERROR("buf overflow");
            m_bufOff = 0;
            return;
        }
    }

    void HandleClose(shared_ptr<MsEvent> evt) override
    {
        // Handle RTP packet close
        m_source->DelEvent(evt);
        m_source->ActiveClose();
    }

private:
    int8_t m_isTcp = -1;
    char *m_buf;
    int m_bufSize;
    int m_bufOff;
    shared_ptr<MsGbSource> m_source;
};

class MsGbRtpAcceptor : public MsEventHandler
{
public:
    MsGbRtpAcceptor(shared_ptr<MsGbSource> source)
        : m_source(source)
    {
    }
    ~MsGbRtpAcceptor()
    {
        MS_LOG_INFO("~MsGbRtpAcceptor");
    }

public:
    void HandleRead(shared_ptr<MsEvent> evt) override
    {
        MsSocket *s = evt->GetSocket();
        shared_ptr<MsSocket> clientSock;
        int rv = s->Accept(clientSock);
        if (rv < 0)
        {
            MS_LOG_ERROR("accept rtp tcp conn error");
            return;
        }

        if (m_source)
        {
            MS_LOG_INFO("rtp tcp accepted socket");
            shared_ptr<MsEventHandler> rtpHandler = make_shared<MsGbRtpHandler>(
                m_source);

            shared_ptr<MsEvent> rtpEvt = make_shared<MsEvent>(
                clientSock, MS_FD_READ | MS_FD_CLOSE, rtpHandler);

            m_source->AddEvent(rtpEvt);
            m_source->DelEvent(evt);
            m_source = nullptr;
        }
    }

    void HandleClose(shared_ptr<MsEvent> evt) override
    {
        // Handle RTP packet close
        if (m_source)
        {
            m_source->DelEvent(evt);
            m_source->ActiveClose();
            m_source = nullptr;
        }
    }

private:
    shared_ptr<MsGbSource> m_source;
};

class MsGbRtpConnector : public MsEventHandler
{
public:
    MsGbRtpConnector(shared_ptr<MsGbSource> source)
        : m_source(source) {}

    ~MsGbRtpConnector()
    {
        MS_LOG_INFO("~MsGbRtpConnector");
    }

    void HandleRead(shared_ptr<MsEvent> evt) override
    {
        // should not read
    }

    void HandleClose(shared_ptr<MsEvent> evt) override
    {
        if (m_source)
        {
            m_source->DelEvent(evt);
            m_source->ActiveClose();
            m_source = nullptr;
        }
    }

    void HandleWrite(shared_ptr<MsEvent> evt) override
    {
        if (m_source)
        {
            MS_LOG_INFO("rtp tcp connected");
            shared_ptr<MsSocket> sock = evt->GetSharedSocket();
            shared_ptr<MsEventHandler> evtHandler = make_shared<MsGbRtpHandler>(m_source);
            shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(sock,
                                                               MS_FD_READ | MS_FD_CLOSE,
                                                               evtHandler);

            m_source->AddEvent(msEvent);
            m_source->DelEvent(evt);
            m_source = nullptr;
        }
    }

private:
    shared_ptr<MsGbSource> m_source;
};

void MsGbSource::Work()
{
    MsMediaSource::Work();

    std::thread worker([this]()
                       { this->OnRun(); });
    worker.detach();
}

void MsGbSource::Exit()
{
    if (m_ctx)
    {
        MsMsg bye;
        bye.m_msgID = MS_STOP_INVITE_CALL;
        bye.m_strVal = m_ctx->gbCallID;
        bye.m_dstType = MS_GB_SERVER;
        bye.m_dstID = 1;
        this->PostMsg(bye);

        delete m_ctx;
        m_ctx = nullptr;
    }

    if (m_psThread)
    {
        m_psThread->join();
        m_psThread.reset();
    }

    if (m_rtpSock)
    {
        m_rtpSock.reset();
    }

    MsMediaSource::Exit();
}

void MsGbSource::HandleMsg(MsMsg &msg)
{
    switch (msg.m_msgID)
    {
    case MS_INVITE_CALL_RSP:
    {
        MS_LOG_INFO("gb source invite rsp:%s code:%d", m_ctx->gbID.c_str(), msg.m_intVal);
        if (msg.m_intVal == 100)
        {
            m_ctx->gbCallID = msg.m_strVal;
        }
        else if (msg.m_intVal == 200)
        {
            // how to deal with dup 200 OK?
            if (m_psThread)
            {
                MS_LOG_WARN("gb source invite rsp:%s duplicate 200 OK", m_ctx->gbID.c_str());
                return;
            }

            // Success
            int transport, port;
            int xtransport = m_ctx->transport;
            string ip;
            const char *p = msg.m_strVal.c_str();
            const char *p1 = strstr(p, "TCP/RTP/AVP");
            const char *p2;

            if (p1)
            {
                p1 = strstr(p, "a=setup:active");
                if (p1)
                {
                    transport = EN_TCP_PASSIVE;
                }
                else
                {
                    p1 = strstr(p, "a=setup:passive");
                    if (p1)
                    {
                        transport = EN_TCP_ACTIVE;
                    }
                    else
                    {
                        goto err;
                    }
                }
            }
            else
            {
                transport = EN_UDP;
            }

            p1 = strstr(p, "c=IN IP4 ");
            if (!p1)
            {
                goto err;
            }
            p1 += strlen("c=IN IP4 ");
            p2 = p1;
            while (*p2 != '\r' && *p2 != '\n' && *p2 != '\0')
            {
                ++p2;
            }

            ip.assign(p1, p2 - p1);

            p1 = strstr(p, "m=video ");
            if (!p1)
            {
                goto err;
            }
            p1 += strlen("m=video ");
            port = atoi(p1);

            if (transport != xtransport)
            {
                goto err;
            }

            if (xtransport == EN_TCP_ACTIVE)
            {
                MsInetAddr addr(AF_INET, ip, port);
                int ret = m_rtpSock->Connect(addr);

                if (ret)
                {
                    if (MS_LAST_ERROR == EAGAIN || MS_LAST_ERROR == EINPROGRESS)
                    {
                        MS_LOG_INFO("rtsp connect %s:%d again err:%d",
                                    ip.c_str(), port, MS_LAST_ERROR);

                        shared_ptr<MsEventHandler> evtHandler = make_shared<MsGbRtpConnector>(
                            dynamic_pointer_cast<MsGbSource>(shared_from_this()));
                        shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(
                            m_rtpSock, MS_FD_CONNECT | MS_FD_CLOSE, evtHandler);
                        this->AddEvent(msEvent);
                        m_rtpSock = nullptr;
                    }
                    else
                    {
                        MS_LOG_WARN("rtsp connect %s:%d err:%d", ip.c_str(), port, MS_LAST_ERROR);
                        goto err;
                    }
                }
                else
                {
                    shared_ptr<MsEventHandler> evtHandler = make_shared<MsGbRtpHandler>(
                        dynamic_pointer_cast<MsGbSource>(shared_from_this()));
                    shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(
                        m_rtpSock, MS_FD_READ | MS_FD_CLOSE, evtHandler);
                    this->AddEvent(msEvent);
                    m_rtpSock = nullptr;
                }
            }

            MS_LOG_INFO("gb source invite:%s call:%s transport:%d ip:%s:%d", m_ctx->gbID.c_str(),
                        m_ctx->gbCallID.c_str(), xtransport, ip.c_str(), port);

            if (!m_psThread)
            {
                m_psThread = std::make_unique<std::thread>(&MsGbSource::PsParseThread, this);
            }

            return;

        err:
            MS_LOG_WARN("gb source invite:%s call:%s transport:%d sdp:%s", m_ctx->gbID.c_str(),
                        m_ctx->gbCallID.c_str(), xtransport, p);

            this->ActiveClose();
        }
        else
        { // Failed
            this->ActiveClose();
        }
    }
    break;

    default:
        MsMediaSource::HandleMsg(msg);
        break;
    }
}

void MsGbSource::OnRun()
{
    int rtpPort;
    int transport = MsConfig::Instance()->GetConfigInt("rtpTransport");
    string rtpIP = MsConfig::Instance()->GetConfigStr("localBindIP");

    shared_ptr<MsSocket> rtpSock = MsPortAllocator::Instance()->AllocPort(
        transport == EN_UDP ? SOCK_DGRAM : SOCK_STREAM, rtpIP, rtpPort);

    if (transport == EN_UDP)
    {
        shared_ptr<MsEventHandler> rtpHandler = make_shared<MsGbRtpHandler>(
            dynamic_pointer_cast<MsGbSource>(shared_from_this()));

        shared_ptr<MsEvent> rtpEvt = make_shared<MsEvent>(
            rtpSock, MS_FD_READ | MS_FD_CLOSE, rtpHandler);

        this->AddEvent(rtpEvt);
    }
    else if (transport == EN_TCP_PASSIVE)
    {
        rtpSock->Listen();

        shared_ptr<MsEventHandler> rtpHandler = make_shared<MsGbRtpAcceptor>(
            dynamic_pointer_cast<MsGbSource>(shared_from_this()));

        shared_ptr<MsEvent> rtpEvt = make_shared<MsEvent>(
            rtpSock, MS_FD_ACCEPT | MS_FD_CLOSE, rtpHandler);

        this->AddEvent(rtpEvt);
    }
    else // tcp active
    {
        rtpSock->SetNonBlock();
        m_rtpSock = rtpSock;
    }

    m_ctx->rtpIP = rtpIP;
    m_ctx->rtpPort = rtpPort;
    m_ctx->transport = transport;

    MS_LOG_INFO("gb source invite:%s bind transport:%d rtp:%s:%d",
                m_ctx->gbID.c_str(), transport, rtpIP.c_str(), rtpPort);

    string xip = MsConfig::Instance()->GetConfigStr("gbMediaIP");
    if (xip.size())
    {
        m_ctx->rtpIP = xip;
    }

    MsMsg inv;
    inv.m_msgID = MS_INIT_INVITE;
    inv.m_dstType = MS_GB_SERVER;
    inv.m_dstID = 1;
    inv.m_ptr = m_ctx;
    this->PostMsg(inv);
}

void MsGbSource::PsParseThread()
{
    AVFormatContext *fmt_ctx = NULL;
    AVIOContext *avio_ctx = NULL;
    const AVInputFormat *fmt = NULL;
    uint8_t *avio_ctx_buffer = NULL;
    AVPacket *pkt = NULL;
    AVDictionary *options = NULL;
    size_t avio_ctx_buffer_size = 4096;
    int ret = 0;

    if (!(fmt_ctx = avformat_alloc_context()))
    {
        MS_LOG_ERROR("Could not allocate format context");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx_buffer = (uint8_t *)av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer)
    {
        MS_LOG_ERROR("Could not allocate avio context buffer");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx = avio_alloc_context(
        avio_ctx_buffer, avio_ctx_buffer_size, 0, this,
        [](void *opaque, uint8_t *buf, int buf_size) -> int
        {
            MsGbSource *source = static_cast<MsGbSource *>(opaque);
            return source->ReadBuffer(buf, buf_size);
        },
        NULL, NULL);

    if (!avio_ctx)
    {
        MS_LOG_ERROR("Could not allocate avio context");
        av_freep(&avio_ctx_buffer);
        ret = AVERROR(ENOMEM);
        goto end;
    }
    fmt_ctx->pb = avio_ctx;

    fmt = av_find_input_format("mpeg");
    av_dict_set(&options, "analyzeduration", "200000", 0);
    ret = avformat_open_input(&fmt_ctx, NULL, fmt, &options);
    av_dict_free(&options);
    if (ret < 0)
    {
        MS_LOG_ERROR("Could not open input");
        goto end;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0)
    {
        MS_LOG_ERROR("Could not find stream information");
        goto end;
    }

    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret < 0)
    {
        MS_LOG_ERROR("Could not find video stream in input:%s",
                     m_streamID.c_str());
        goto end;
    }

    m_videoIdx = ret;
    m_video = fmt_ctx->streams[m_videoIdx];
    if (m_video->codecpar->codec_id != AV_CODEC_ID_H264 &&
        m_video->codecpar->codec_id != AV_CODEC_ID_H265)
    {
        MS_LOG_ERROR("not support codec:%d url:%s", m_video->codecpar->codec_id,
                     m_streamID.c_str());
        goto end;
    }

    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret >= 0)
    {
        if (fmt_ctx->streams[ret]->codecpar->codec_id == AV_CODEC_ID_AAC)
        {
            m_audioIdx = ret;
            m_audio = fmt_ctx->streams[m_audioIdx];
        }
    }

    this->NotifyStreamInfo();

    pkt = av_packet_alloc();

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, pkt) >= 0 && !m_isClosing.load())
    {
        if (pkt->stream_index == m_videoIdx || pkt->stream_index == m_audioIdx)
        {
            this->NotifyStreamPacket(pkt);
        }
        av_packet_unref(pkt);
    }

end:
    avformat_close_input(&fmt_ctx);

    /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
    if (avio_ctx)
        av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    av_packet_free(&pkt);
    this->ActiveClose();
}

int MsGbSource::ReadBuffer(uint8_t *buf, int buf_size)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condVar.wait(lock, [this]()
                   { return m_bufWriteOff - m_bufReadOff > 0 || m_isClosing.load(); });

    if (m_isClosing.load())
    {
        return AVERROR_EOF;
    }

    if (m_bufWriteOff - m_bufReadOff <= 0)
    {
        return AVERROR(EAGAIN);
    }

    int toRead = buf_size;
    int available = m_bufWriteOff - m_bufReadOff;
    if (toRead > available)
    {
        toRead = available;
    }

    memcpy(buf, m_buf + m_bufReadOff, toRead);
    m_bufReadOff += toRead;
    return toRead;
}

void MsGbSource::WriteBuffer(uint8_t *buf, int len)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_bufWriteOff + len > m_bufSize)
    {
        // if left size can fit new data, move data to head
        if (m_bufSize - m_bufWriteOff + m_bufReadOff >= len)
        {
            memmove(m_buf, m_buf + m_bufReadOff, m_bufWriteOff - m_bufReadOff);
            m_bufWriteOff -= m_bufReadOff;
            m_bufReadOff = 0;
        }
        else
        {
            // need expand buffer
            int newSize = m_bufSize * 2;
            while (m_bufWriteOff + len > newSize)
            {
                newSize *= 2;
            }
            uint8_t *newBuf = new uint8_t[newSize];
            memcpy(newBuf, m_buf + m_bufReadOff, m_bufWriteOff - m_bufReadOff);
            delete[] m_buf;
            m_buf = newBuf;
            m_bufSize = newSize;
            m_bufWriteOff -= m_bufReadOff;
            m_bufReadOff = 0;
        }
    }

    memcpy(m_buf + m_bufWriteOff, buf, len);
    m_bufWriteOff += len;
    m_condVar.notify_one();
}

void MsGbSource::ProcessRtp(uint8_t *buf, int len)
{
    // Process RTP packet
    unsigned int ssrc;
    int payload_type, flags = 0;
    uint16_t seq;
    int ext, csrc;
    uint32_t timestamp;
    int rv = 0;

    csrc = buf[0] & 0x0f;
    ext = buf[0] & 0x10;
    payload_type = buf[1] & 0x7f;
    if (buf[1] & 0x80)
        flags |= RTP_FLAG_MARKER;
    seq = AV_RB16(buf + 2);
    timestamp = AV_RB32(buf + 4);
    ssrc = AV_RB32(buf + 8);

    /* NOTE: we can handle only one payload type */
    if (m_payload != payload_type)
        return;

    if (m_firstPkt)
    {
        m_seq = seq;
        m_firstPkt = false;
    }
    else
    {
        uint16_t expected = m_seq + 1;

        if (seq != expected)
        {
            // pkt loss
            // TODO: add loss statistic
            MS_LOG_WARN("gb source rtp pkt loss, last seq:%d recv seq:%d",
                        m_seq, seq);
        }
    }

    if (buf[0] & 0x20)
    {
        int padding = buf[len - 1];
        if (len >= 12 + padding)
            len -= padding;
    }

    m_seq = seq;

    len -= 12;
    buf += 12;

    len -= 4 * csrc;
    buf += 4 * csrc;

    if (len < 0)
        return;

    /* RFC 3550 Section 5.3.1 RTP Header Extension handling */
    if (ext)
    {
        if (len < 4)
            return;
        /* calculate the header extension length (stored as number
         * of 32-bit words) */
        ext = (AV_RB16(buf + 2) + 1) << 2;

        if (len < ext)
            return;
        // skip past RTP header extension
        len -= ext;
        buf += ext;
    }

    this->WriteBuffer(buf, len);
}