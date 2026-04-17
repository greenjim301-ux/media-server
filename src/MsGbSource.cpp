#include "MsGbSource.h"
#include "MsConfig.h"
#include "MsDevMgr.h"
#include "MsLog.h"
#include "MsPortAllocator.h"
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
}

class MsGbRtpHandler : public MsEventHandler {
public:
	MsGbRtpHandler(shared_ptr<MsGbSource> source)
	    : m_bufPtr(make_unique<char[]>(DEF_BUF_SIZE)), m_bufSize(DEF_BUF_SIZE), m_bufOff(0),
	      m_source(source) {}

	~MsGbRtpHandler() { MS_LOG_INFO("~MsGbRtpHandler"); }

	void HandleRead(shared_ptr<MsEvent> evt) override {
		MsSocket *s = evt->GetSocket();
		int n = s->Recv(m_bufPtr.get() + m_bufOff, m_bufSize - m_bufOff);

		if (n <= 0) {
			return;
		}

		m_bufOff += n;

		if (m_isTcp == -1) {
			m_isTcp = s->IsTcp() ? 1 : 0;
		}

		if (m_isTcp) {
			uint8_t *xbuf = (uint8_t *)m_bufPtr.get();

			while (m_bufOff > 2) {
				int pktLen = AV_RB16(xbuf);

				if (pktLen <= 0) {
					MS_LOG_ERROR("pkt len:%d error", pktLen);
					m_bufOff = 0;
					return;
				}

				if (pktLen > m_bufOff - 2) // not complete
				{
					if (m_bufOff) {
						if (xbuf - (uint8_t *)m_bufPtr.get() != 0) {
							memmove(m_bufPtr.get(), xbuf, m_bufOff);
						}
					}

					return;
				} else {
					if (m_source->ProcessRtp(xbuf + 2, pktLen) < 0) {
						MS_LOG_WARN("gb source closing, stop process rtp");
						m_bufOff = 0;
						m_source->DelEvent(evt);
						m_source->SourceActiveClose();
						return;
					}
					xbuf += pktLen + 2;
					m_bufOff -= pktLen + 2;
				}
			}

			if (m_bufOff) {
				memmove(m_bufPtr.get(), xbuf, m_bufOff);
			}
		} else // udp
		{
			if (m_source->ProcessRtp((uint8_t *)m_bufPtr.get(), m_bufOff) < 0) {
				MS_LOG_WARN("gb source closing, stop process rtp");
				m_bufOff = 0;
				m_source->DelEvent(evt);
				m_source->SourceActiveClose();
				return;
			}
			m_bufOff = 0;
		}

		if (m_bufOff >= m_bufSize) {
			MS_LOG_ERROR("buf overflow");
			m_bufOff = 0;
			return;
		}
	}

	void HandleClose(shared_ptr<MsEvent> evt) override {
		// Handle RTP packet close
		m_source->DelEvent(evt);
		m_source->SourceActiveClose();
	}

private:
	int8_t m_isTcp = -1;
	unique_ptr<char[]> m_bufPtr;
	int m_bufSize;
	int m_bufOff;
	shared_ptr<MsGbSource> m_source;
};

class MsGbRtpAcceptor : public MsEventHandler {
public:
	MsGbRtpAcceptor(shared_ptr<MsGbSource> source) : m_source(source) {}
	~MsGbRtpAcceptor() { MS_LOG_INFO("~MsGbRtpAcceptor"); }

public:
	void HandleRead(shared_ptr<MsEvent> evt) override {
		MsSocket *s = evt->GetSocket();
		shared_ptr<MsSocket> clientSock;
		int rv = s->Accept(clientSock);
		if (rv < 0) {
			MS_LOG_ERROR("accept rtp tcp conn error");
			return;
		}

		if (m_source) {
			MS_LOG_INFO("rtp tcp accepted socket");
			shared_ptr<MsEventHandler> rtpHandler = make_shared<MsGbRtpHandler>(m_source);

			shared_ptr<MsEvent> rtpEvt =
			    make_shared<MsEvent>(clientSock, MS_FD_READ | MS_FD_CLOSE, rtpHandler);

			m_source->AddEvent(rtpEvt);
			m_source->DelEvent(evt);
			m_source = nullptr;
		}
	}

	void HandleClose(shared_ptr<MsEvent> evt) override {
		// Handle RTP packet close
		if (m_source) {
			m_source->DelEvent(evt);
			m_source->SourceActiveClose();
			m_source = nullptr;
		}
	}

private:
	shared_ptr<MsGbSource> m_source;
};

class MsGbRtpConnector : public MsEventHandler {
public:
	MsGbRtpConnector(shared_ptr<MsGbSource> source) : m_source(source) {}

	~MsGbRtpConnector() { MS_LOG_INFO("~MsGbRtpConnector"); }

	void HandleRead(shared_ptr<MsEvent> evt) override {
		// should not read
	}

	void HandleClose(shared_ptr<MsEvent> evt) override {
		if (m_source) {
			m_source->DelEvent(evt);
			m_source->SourceActiveClose();
			m_source = nullptr;
		}
	}

	void HandleWrite(shared_ptr<MsEvent> evt) override {
		if (m_source) {
			MS_LOG_INFO("rtp tcp connected");
			shared_ptr<MsSocket> sock = evt->GetSharedSocket();
			shared_ptr<MsEventHandler> evtHandler = make_shared<MsGbRtpHandler>(m_source);
			shared_ptr<MsEvent> msEvent =
			    make_shared<MsEvent>(sock, MS_FD_READ | MS_FD_CLOSE, evtHandler);

			m_source->AddEvent(msEvent);
			m_source->DelEvent(evt);
			m_source = nullptr;
		}
	}

private:
	shared_ptr<MsGbSource> m_source;
};

void MsGbSource::Work() {
	MsReactor::Run();

	std::thread worker([this]() { this->OnRun(); });
	worker.detach();
}

void MsGbSource::Exit() {
	if (m_ctx) {
		MsMsg bye;
		bye.m_msgID = MS_STOP_INVITE_CALL;
		bye.m_strVal = m_ctx->gbCallID;
		bye.m_dstType = MS_GB_SERVER;
		bye.m_dstID = 1;
		this->PostMsg(bye);

		m_ctx = nullptr;
	}

	if (m_psThread) {
		if (m_psThread->joinable())
			m_psThread->join();
		m_psThread.reset();
	}

	if (m_rtpSock) {
		m_rtpSock.reset();
	}

	MsReactor::Exit();
}

void MsGbSource::HandleMsg(MsMsg &msg) {
	switch (msg.m_msgID) {
	case MS_INVITE_CALL_RSP: {
		MS_LOG_INFO("gb source invite rsp:%s code:%d", m_ctx->gbID.c_str(), msg.m_intVal);
		if (msg.m_intVal == 100) {
			m_ctx->gbCallID = msg.m_strVal;
		} else if (msg.m_intVal == 200) {
			// how to deal with dup 200 OK?
			if (m_psThread) {
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

			if (p1) {
				p1 = strstr(p, "a=setup:active");
				if (p1) {
					transport = EN_TCP_PASSIVE;
				} else {
					p1 = strstr(p, "a=setup:passive");
					if (p1) {
						transport = EN_TCP_ACTIVE;
					} else {
						goto err;
					}
				}
			} else {
				transport = EN_UDP;
			}

			p1 = strstr(p, "c=IN IP4 ");
			if (!p1) {
				goto err;
			}
			p1 += strlen("c=IN IP4 ");
			p2 = p1;
			while (*p2 != '\r' && *p2 != '\n' && *p2 != '\0') {
				++p2;
			}

			ip.assign(p1, p2 - p1);

			p1 = strstr(p, "m=video ");
			if (!p1) {
				goto err;
			}
			p1 += strlen("m=video ");
			port = atoi(p1);

			if (transport != xtransport) {
				goto err;
			}

			if (xtransport == EN_TCP_ACTIVE) {
				MsInetAddr addr(AF_INET, ip, port);
				int ret = m_rtpSock->Connect(addr);

				if (ret) {
					if (MS_LAST_ERROR == EAGAIN || MS_LAST_ERROR == EINPROGRESS) {
						MS_LOG_INFO("rtsp connect %s:%d again err:%d", ip.c_str(), port,
						            MS_LAST_ERROR);

						shared_ptr<MsEventHandler> evtHandler = make_shared<MsGbRtpConnector>(
						    dynamic_pointer_cast<MsGbSource>(shared_from_this()));
						shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(
						    m_rtpSock, MS_FD_CONNECT | MS_FD_CLOSE, evtHandler);
						this->AddEvent(msEvent);
						m_rtpSock = nullptr;
					} else {
						MS_LOG_WARN("rtsp connect %s:%d err:%d", ip.c_str(), port, MS_LAST_ERROR);
						goto err;
					}
				} else {
					shared_ptr<MsEventHandler> evtHandler = make_shared<MsGbRtpHandler>(
					    dynamic_pointer_cast<MsGbSource>(shared_from_this()));
					shared_ptr<MsEvent> msEvent =
					    make_shared<MsEvent>(m_rtpSock, MS_FD_READ | MS_FD_CLOSE, evtHandler);
					this->AddEvent(msEvent);
					m_rtpSock = nullptr;
				}
			}

			MS_LOG_INFO("gb source invite:%s call:%s transport:%d ip:%s:%d", m_ctx->gbID.c_str(),
			            m_ctx->gbCallID.c_str(), xtransport, ip.c_str(), port);

			if (!m_psThread) {
				m_psThread = std::make_unique<std::thread>(&MsGbSource::PsParseThread, this);
			}

			return;

		err:
			MS_LOG_WARN("gb source invite:%s call:%s transport:%d sdp:%s", m_ctx->gbID.c_str(),
			            m_ctx->gbCallID.c_str(), xtransport, p);

			this->SourceActiveClose();
		} else { // Failed
			this->SourceActiveClose();
		}
	} break;

	default:
		MsReactor::HandleMsg(msg);
		break;
	}
}

void MsGbSource::OnRun() {
	int rtpPort;
	int transport = MsConfig::Instance()->GetConfigInt("rtpTransport");
	string rtpIP = MsConfig::Instance()->GetConfigStr("localBindIP");

	shared_ptr<MsSocket> rtpSock = MsPortAllocator::Instance()->AllocPort(
	    transport == EN_UDP ? SOCK_DGRAM : SOCK_STREAM, rtpIP, rtpPort);

	if (transport == EN_UDP) {
		shared_ptr<MsEventHandler> rtpHandler =
		    make_shared<MsGbRtpHandler>(dynamic_pointer_cast<MsGbSource>(shared_from_this()));

		shared_ptr<MsEvent> rtpEvt =
		    make_shared<MsEvent>(rtpSock, MS_FD_READ | MS_FD_CLOSE, rtpHandler);

		this->AddEvent(rtpEvt);
	} else if (transport == EN_TCP_PASSIVE) {
		rtpSock->Listen();

		shared_ptr<MsEventHandler> rtpHandler =
		    make_shared<MsGbRtpAcceptor>(dynamic_pointer_cast<MsGbSource>(shared_from_this()));

		shared_ptr<MsEvent> rtpEvt =
		    make_shared<MsEvent>(rtpSock, MS_FD_ACCEPT | MS_FD_CLOSE, rtpHandler);

		this->AddEvent(rtpEvt);
	} else // tcp active
	{
		rtpSock->SetNonBlock();
		m_rtpSock = rtpSock;
	}

	m_ctx->rtpIP = rtpIP;
	m_ctx->rtpPort = rtpPort;
	m_ctx->transport = transport;

	MS_LOG_INFO("gb source invite:%s bind transport:%d rtp:%s:%d", m_ctx->gbID.c_str(), transport,
	            rtpIP.c_str(), rtpPort);

	string xip = MsConfig::Instance()->GetConfigStr("gbMediaIP");
	if (xip.size()) {
		m_ctx->rtpIP = xip;
	}

	MsMsg inv;
	inv.m_msgID = MS_INIT_INVITE;
	inv.m_dstType = MS_GB_SERVER;
	inv.m_dstID = 1;
	inv.m_any = m_ctx;
	this->PostMsg(inv);
}

void MsGbSource::PsParseThread() {
	AVFormatContext *fmt_ctx = NULL;
	AVIOContext *avio_ctx = NULL;
	const AVInputFormat *fmt = NULL;
	uint8_t *avio_ctx_buffer = NULL;
	AVPacket *pkt = NULL;
	AVDictionary *options = NULL;
	size_t avio_ctx_buffer_size = 4096;
	int ret = 0;

	if (!(fmt_ctx = avformat_alloc_context())) {
		MS_LOG_ERROR("Could not allocate format context");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	avio_ctx_buffer = (uint8_t *)av_malloc(avio_ctx_buffer_size);
	if (!avio_ctx_buffer) {
		MS_LOG_ERROR("Could not allocate avio context buffer");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	avio_ctx = avio_alloc_context(
	    avio_ctx_buffer, avio_ctx_buffer_size, 0, this,
	    [](void *opaque, uint8_t *buf, int buf_size) -> int {
		    MsGbSource *source = static_cast<MsGbSource *>(opaque);
		    return source->ReadBuffer(buf, buf_size);
	    },
	    NULL, NULL);

	if (!avio_ctx) {
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
	if (ret < 0) {
		MS_LOG_ERROR("Could not open input");
		goto end;
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0) {
		MS_LOG_ERROR("Could not find stream information");
		goto end;
	}

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (ret < 0) {
		MS_LOG_ERROR("Could not find video stream in input:%s", m_streamID.c_str());
		goto end;
	}

	m_videoIdx = ret;
	m_video = fmt_ctx->streams[m_videoIdx];
	if (m_video->codecpar->codec_id != AV_CODEC_ID_H264 &&
	    m_video->codecpar->codec_id != AV_CODEC_ID_H265) {
		MS_LOG_ERROR("not support codec:%d url:%s", m_video->codecpar->codec_id,
		             m_streamID.c_str());
		goto end;
	}

	if (m_video->duration == 0 || m_video->duration == AV_NOPTS_VALUE) {
		int64_t st = stoll(m_ctx->startTime);
		int64_t et = stoll(m_ctx->endTime);
		if (et > st) {
			m_video->duration = (et - st) * m_video->time_base.den / m_video->time_base.num;
		}
	}

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (ret >= 0) {
		if (fmt_ctx->streams[ret]->codecpar->codec_id == AV_CODEC_ID_AAC ||
		    fmt_ctx->streams[ret]->codecpar->codec_id == AV_CODEC_ID_PCM_ALAW ||
		    fmt_ctx->streams[ret]->codecpar->codec_id == AV_CODEC_ID_OPUS) {
			m_audioIdx = ret;
			m_audio = fmt_ctx->streams[m_audioIdx];
		}
	}

	this->NotifyStreamInfo();

	pkt = av_packet_alloc();

	/* read frames from the file */
	while (av_read_frame(fmt_ctx, pkt) >= 0 && !m_isClosing.load()) {
		if (pkt->stream_index == m_videoIdx || pkt->stream_index == m_audioIdx) {
			// if (pkt->stream_index == m_videoIdx) {
			// 	MS_LOG_DEBUG("gb source video pkt pts:%ld dts:%ld key:%d", pkt->pts, pkt->dts,
			// 	             pkt->flags & AV_PKT_FLAG_KEY);
			// } else if (pkt->stream_index == m_audioIdx) {
			// 	MS_LOG_DEBUG("gb source audio pkt pts:%ld dts:%ld size:%d", pkt->pts, pkt->dts,
			// 	             pkt->size);
			// }
			this->NotifyStreamPacket(pkt);
		}
		av_packet_unref(pkt);
	}

end:
	if (fmt_ctx) {
		if (fmt_ctx->pb) {
			avio_flush(fmt_ctx->pb);
			av_freep(&fmt_ctx->pb->buffer);
			avio_context_free(&fmt_ctx->pb);
		}
		avformat_free_context(fmt_ctx);
		fmt_ctx = nullptr;
	}
	av_packet_free(&pkt);
	if (!m_isClosing.load())
		this->SourceActiveClose();
}

int MsGbSource::ReadBuffer(uint8_t *buf, int buf_size) {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_condVar.wait(lock, [this]() { return m_ringBuffer->size() > 0 || m_isClosing.load(); });

	if (m_isClosing.load()) {
		return AVERROR_EOF;
	}

	if (m_ringBuffer->size() <= 0) {
		return AVERROR(EAGAIN);
	}

	return m_ringBuffer->read((char *)buf, buf_size);
}

int MsGbSource::WriteBuffer(uint8_t *buf, int len) {
	if (m_isClosing.load()) {
		return -1;
	}

	std::unique_lock<std::mutex> lock(m_mutex);
	m_ringBuffer->write(buf, len);
	lock.unlock();
	m_condVar.notify_one();
	return 0;
}

int MsGbSource::ProcessRtp(uint8_t *buf, int len) {
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
		return 0;

	if (m_firstPkt) {
		m_seq = seq;
		m_firstPkt = false;
	} else {
		uint16_t expected = m_seq + 1;

		if (seq != expected) {
			// pkt loss
			// TODO: add loss problem handling
			MS_LOG_WARN("gb source rtp pkt loss, last seq:%d recv seq:%d", m_seq, seq);
		}
	}

	if (buf[0] & 0x20) {
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
		return 0;

	/* RFC 3550 Section 5.3.1 RTP Header Extension handling */
	if (ext) {
		if (len < 4)
			return 0;
		/* calculate the header extension length (stored as number
		 * of 32-bit words) */
		ext = (AV_RB16(buf + 2) + 1) << 2;

		if (len < ext)
			return 0;
		// skip past RTP header extension
		len -= ext;
		buf += ext;
	}

	return this->WriteBuffer(buf, len);
}

void MsGbSource::UpdateVideoInfo() {
	if (m_ctx && m_video && m_ctx->type == 0) {
		ModDev m;
		m.m_codec = avcodec_get_name(m_video->codecpar->codec_id);
		m.m_resolution =
		    to_string(m_video->codecpar->width) + "x" + to_string(m_video->codecpar->height);
		MsDevMgr::Instance()->ModifyDevice(m_ctx->gbID, m);
	}
}

void MsGbSource::SourceActiveClose() {
	m_isClosing.store(true);
	m_condVar.notify_all();
	MsMediaSource::SourceActiveClose();
	this->PostExit();
}

void MsGbSource::OnSinksEmpty() {
	m_isClosing.store(true);
	m_condVar.notify_all();
	MsMediaSource::OnSinksEmpty();
}
