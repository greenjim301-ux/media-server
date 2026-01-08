#include "MsRtspSink.h"
#include "MsConfig.h"
#include "MsEvent.h"
#include "MsLog.h"
#include "MsSourceFactory.h"
#include <thread>

class MsRtspHandler : public MsEventHandler {
public:
	MsRtspHandler(shared_ptr<MsReactor> reactor, shared_ptr<MsIRtspServer> iServer)
	    : m_reactor(reactor), m_iServer(iServer), m_bufSize(64 * 1024), m_bufOff(0) {
		m_buf = (char *)malloc(m_bufSize);
	}

	~MsRtspHandler() {
		MS_LOG_INFO("~MsRtspHandler");
		free(m_buf);
	}

	void HandleRead(shared_ptr<MsEvent> evt) {
		MsSocket *sock = evt->GetSocket();

		int recv = sock->Recv(m_buf + m_bufOff, m_bufSize - 1 - m_bufOff);

		if (recv <= 0) {
			return;
		}

		m_bufOff += recv;
		m_buf[m_bufOff] = '\0';
		char *p2 = m_buf;

		while (m_bufOff > 0) {
			if (!IsHeaderComplete(p2)) {
				if (m_bufOff == m_bufSize - 1) {
					MS_LOG_ERROR("rtsp header too big:%d %s", m_bufOff, m_buf);
					m_bufOff = 0;
				} else if (m_bufOff && p2 != m_buf) {
					memmove(m_buf, p2, m_bufOff);
				}

				return;
			}

			char *oriP2 = p2;
			MsRtspMsg rtspMsg;

			rtspMsg.Parse(p2);

			int cntLen = rtspMsg.m_contentLength.GetIntVal();
			int left = m_bufOff - (p2 - oriP2);
			int ret = 0;

			if (left < cntLen) {
				p2 = oriP2;

				if (m_bufOff && p2 != m_buf) {
					memmove(m_buf, p2, m_bufOff);
				}

				return;
			}

			MS_LOG_VERBS("recv:%s", m_buf);

			if (rtspMsg.m_method == "OPTIONS") {
				ret = m_iServer->HandleOptions(rtspMsg, evt);
			} else if (rtspMsg.m_method == "DESCRIBE") {
				ret = m_iServer->HandleDescribe(rtspMsg, evt);
			} else if (rtspMsg.m_method == "SETUP") {
				ret = m_iServer->HandleSetup(rtspMsg, evt);
			} else if (rtspMsg.m_method == "PLAY") {
				ret = m_iServer->HandlePlay(rtspMsg, evt);
			} else if (rtspMsg.m_method == "TEARDOWN") {
				ret = m_iServer->HandleTeardown(rtspMsg, evt);
			} else if (rtspMsg.m_method == "PAUSE") {
				ret = m_iServer->HandlePause(rtspMsg, evt);
			} else if (rtspMsg.m_method == "SET_PARAMETER" || rtspMsg.m_method == "GET_PARAMETER") {
				ret = m_iServer->HandleOthers(rtspMsg, evt);
			} else {
				MS_LOG_ERROR("unknown method:%s", rtspMsg.m_method.c_str());
			}

			if (ret != 0) {
				MS_LOG_WARN("handle rtsp method:%s failed", rtspMsg.m_method.c_str());
				m_reactor->DelEvent(evt);
				return;
			}

			p2 += cntLen;
			m_bufOff -= (p2 - oriP2);
		}
	}

	void HandleClose(shared_ptr<MsEvent> evt) {
		m_reactor->DelEvent(evt);
		m_iServer->OnCloseEvent(evt);
	}

	void HandleWrite(shared_ptr<MsEvent> evt) { m_iServer->OnWriteEvent(evt); }

private:
	shared_ptr<MsReactor> m_reactor;
	shared_ptr<MsIRtspServer> m_iServer;
	char *m_buf;
	int m_bufSize;
	int m_bufOff;
};

class MsRtspAcceptHandler : public MsEventHandler {
public:
	MsRtspAcceptHandler(shared_ptr<MsReactor> reactor) : m_reactor(reactor) {}

	void HandleRead(shared_ptr<MsEvent> evt) {
		MsSocket *sock = evt->GetSocket();
		shared_ptr<MsSocket> s;

		if (sock->Accept(s)) {
			MS_LOG_WARN("rstp accept err:%d", MS_LAST_ERROR);
			return;
		}

		shared_ptr<MsEventHandler> evtHandler =
		    make_shared<MsRtspHandler>(m_reactor, dynamic_pointer_cast<MsIRtspServer>(m_reactor));
		shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(s, MS_FD_READ | MS_FD_CLOSE, evtHandler);
		m_reactor->AddEvent(msEvent);
	}

	void HandleClose(shared_ptr<MsEvent> evt) {
		m_reactor->DelEvent(evt);
		m_reactor->PostExit();
	}

private:
	shared_ptr<MsReactor> m_reactor;
};

void MsRtspServer::Run() {
	MsReactor::Run();

	MsConfig *config = MsConfig::Instance();

	string ip = config->GetConfigStr("rtspIP");
	int port = config->GetConfigInt("rtspPort");

	MsInetAddr bindAddr(AF_INET, ip, port);
	shared_ptr<MsSocket> sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);

	if (0 != sock->Bind(bindAddr)) {
		MS_LOG_ERROR("rtsp bind %s:%d err:%d", ip.c_str(), port, MS_LAST_ERROR);
		return;
	}

	if (0 != sock->Listen()) {
		MS_LOG_ERROR("rtsp listen %s:%d err:%d", ip.c_str(), port, MS_LAST_ERROR);
		return;
	}

	MS_LOG_INFO("rtsp listen:%s:%d", ip.c_str(), port);

	shared_ptr<MsEventHandler> evtHandler = make_shared<MsRtspAcceptHandler>(shared_from_this());
	shared_ptr<MsEvent> evt = make_shared<MsEvent>(sock, MS_FD_ACCEPT, evtHandler);

	this->AddEvent(evt);

	std::thread worker(&MsReactor::Wait, shared_from_this());
	worker.detach();
}

int MsRtspServer::HandleOptions(MsRtspMsg &msg, shared_ptr<MsEvent> evt) {
	MsRtspMsg rsp;

	rsp.m_status = "200";
	rsp.m_reason = "OK";
	rsp.m_version = msg.m_version;
	rsp.m_cseq = msg.m_cseq;
	rsp.m_public.SetValue("OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, "
	                      "SET_PARAMETER, GET_PARAMETER");

	SendRtspMsg(rsp, evt->GetSocket());
	return 0;
}

int MsRtspServer::HandleDescribe(MsRtspMsg &msg, shared_ptr<MsEvent> evt) {
	MsRtspMsg rsp;
	rsp.m_version = msg.m_version;
	rsp.m_cseq = msg.m_cseq;

	size_t p = msg.m_uri.find("/", strlen("rtsp://"));
	if (p == string::npos) {
		MS_LOG_WARN("invalid uri:%s", msg.m_uri.c_str());
		rsp.m_status = "404";
		rsp.m_reason = "Not Found";
		SendRtspMsg(rsp, evt->GetSocket());
		return -1;
	}
	msg.m_uri = msg.m_uri.substr(p);

	std::vector<std::string> s = SplitString(msg.m_uri, "/");
	if (s.size() < 3) {
		MS_LOG_WARN("invalid uri:%s", msg.m_uri.c_str());
		rsp.m_status = "404";
		rsp.m_reason = "Not Found";
		SendRtspMsg(rsp, evt->GetSocket());
		return -1;
	}

	if (s[1] == "live") {
		std::string streamID = s[2];
		std::string format = "rtsp";

		std::shared_ptr<MsMeidaSink> sink =
		    std::make_shared<MsRtspSink>(format, streamID, ++m_seqID, evt->GetSharedSocket(), msg);

		std::shared_ptr<MsMediaSource> source =
		    MsResManager::GetInstance().GetMediaSource(streamID);

		if (source) {
			source->AddSink(sink);
		} else {
			source = MsSourceFactory::CreateLiveSource(streamID);
			if (!source) {
				MS_LOG_WARN("create source failed for stream: %s", streamID.c_str());
				rsp.m_status = "404";
				rsp.m_reason = "Not Found";
				SendRtspMsg(rsp, evt->GetSocket());
				return -1;
			}
			source->AddSink(sink);
			source->Work();
		}

		this->DelEvent(evt);
		return 0;
	} else if (s[1] == "vod") {
		if (s.size() < 4) {
			MS_LOG_WARN("invalid vod uri:%s", msg.m_uri.c_str());
			rsp.m_status = "404";
			rsp.m_reason = "Not Found";
			SendRtspMsg(rsp, evt->GetSocket());
			return -1;
		}

		std::string streamID = s[2];
		std::string filename = s[3];
		std::string format = "rtsp";

		// check media source exists
		auto source = MsResManager::GetInstance().GetMediaSource(streamID);
		if (source) {
			MS_LOG_WARN("vod source already exists for stream: %s", streamID.c_str());
			rsp.m_status = "403";
			rsp.m_reason = "Forbidden";
			SendRtspMsg(rsp, evt->GetSocket());
			return -1;
		}

		std::shared_ptr<MsMeidaSink> sink =
		    std::make_shared<MsRtspSink>(format, streamID, ++m_seqID, evt->GetSharedSocket(), msg);

		source = MsSourceFactory::CreateVodSource(streamID, filename);
		if (!source) {
			MS_LOG_WARN("create VOD source failed for stream: %s", streamID.c_str());
			rsp.m_status = "404";
			rsp.m_reason = "Not Found";
			SendRtspMsg(rsp, evt->GetSocket());
			return -1;
		}
		source->AddSink(sink);
		source->Work();

		this->DelEvent(evt);
		return 0;
	} else if (s[1] == "gbvod") {
		if (s.size() < 4) {
			MS_LOG_WARN("invalid gbvod uri:%s", msg.m_uri.c_str());
			rsp.m_status = "404";
			rsp.m_reason = "Not Found";
			SendRtspMsg(rsp, evt->GetSocket());
			return -1;
		}

		std::string streamID = s[2];
		std::string streamInfo = s[3];
		std::string format = "rtsp";

		// check media source exists
		auto source = MsResManager::GetInstance().GetMediaSource(streamID);
		if (source) {
			MS_LOG_WARN("gbvod source already exists for stream: %s", streamID.c_str());
			rsp.m_status = "403";
			rsp.m_reason = "Forbidden";
			SendRtspMsg(rsp, evt->GetSocket());
			return -1;
		}

		std::shared_ptr<MsMeidaSink> sink =
		    std::make_shared<MsRtspSink>(format, streamID, ++m_seqID, evt->GetSharedSocket(), msg);

		source = MsSourceFactory::CreateGbvodSource(streamID, streamInfo);
		if (!source) {
			MS_LOG_WARN("create GBVOD source failed for stream: %s", streamID.c_str());
			rsp.m_status = "404";
			rsp.m_reason = "Not Found";
			SendRtspMsg(rsp, evt->GetSocket());
			return -1;
		}
		source->AddSink(sink);
		source->Work();

		this->DelEvent(evt);
		return 0;
	} else {
		MS_LOG_WARN("unsupported uri:%s", msg.m_uri.c_str());
		rsp.m_status = "404";
		rsp.m_reason = "Not Found";
		SendRtspMsg(rsp, evt->GetSocket());
		return -1;
	}
}

int MsRtspSink::HandleSetup(MsRtspMsg &msg, shared_ptr<MsEvent> evt) {
	MsRtspMsg rsp;
	rsp.m_version = msg.m_version;
	rsp.m_cseq = msg.m_cseq;
	size_t p = msg.m_transport.m_value.find("TCP");

	if (p == string::npos) {
		rsp.m_status = "461";
		rsp.m_reason = "Unsupported transport";
	} else {
		size_t p = msg.m_uri.find("streamid=0");
		int channel = 0;
		if (p == string::npos) {
			p = msg.m_uri.find("streamid=1");
			if (p == string::npos) {
				// error, no streamid
				rsp.m_status = "400";
				rsp.m_reason = "Bad Request";
				string data;
				rsp.Dump(data);
				this->WriteBuffer((const uint8_t *)data.c_str(), data.size(), -1);
				return 0;
			} else {
				channel = 1;
			}
		}

		char buf[64];
		snprintf(buf, sizeof(buf), "RTP/AVP/TCP;unicast;interleaved=%d-%d", channel * 2,
		         channel * 2 + 1);
		rsp.m_transport.SetValue(buf);
		rsp.m_status = "200";
		rsp.m_reason = "OK";

		if (msg.m_session.m_exist) {
			rsp.m_session = msg.m_session;
		} else {
			m_session = GenRandStr(16);
			rsp.m_session.SetValue(m_session);
		}
	}

	string data;
	rsp.Dump(data);
	this->WriteBuffer((const uint8_t *)data.c_str(), data.size(), -1);
	return 0;
}

int MsRtspSink::HandlePlay(MsRtspMsg &msg, shared_ptr<MsEvent> evt) {
	MsRtspMsg rsp;
	rsp.m_version = msg.m_version;
	rsp.m_cseq = msg.m_cseq;
	rsp.m_status = "200";
	rsp.m_reason = "OK";
	rsp.m_session = msg.m_session;
	string data;
	rsp.Dump(data);
	this->WriteBuffer((const uint8_t *)data.c_str(), data.size(), -1);

	if (!m_playing) {
		if (m_queData.size() > 0) {
			// regist write
			m_evt->SetEvent(MS_FD_READ | MS_FD_CLOSE | MS_FD_CONNECT);
			m_reactor->ModEvent(m_evt);

			MS_LOG_INFO("sink %s:%d regist write", m_type.c_str(), m_sinkID);
		}

		m_playing = true;
	}

	return 0;
}

int MsRtspSink::HandleTeardown(MsRtspMsg &msg, shared_ptr<MsEvent> evt) {
	MsRtspMsg rsp;
	rsp.m_version = msg.m_version;
	rsp.m_cseq = msg.m_cseq;
	rsp.m_status = "200";
	rsp.m_reason = "OK";
	rsp.m_session = msg.m_session;
	string data;
	rsp.Dump(data);

	this->WriteBuffer((const uint8_t *)data.c_str(), data.size(), -1);
	this->ActiveClose();
	return 0;
}

int MsRtspSink::HandleOthers(MsRtspMsg &msg, shared_ptr<MsEvent> evt) {
	MsRtspMsg rsp;
	string data;
	rsp.m_version = msg.m_version;
	rsp.m_cseq = msg.m_cseq;
	rsp.m_status = "200";
	rsp.m_reason = "OK";
	rsp.m_session = msg.m_session;
	rsp.Dump(data);
	this->WriteBuffer((const uint8_t *)data.c_str(), data.size(), -1);
	return 0;
}

int MsRtspSink::HandlePause(MsRtspMsg &msg, shared_ptr<MsEvent> evt) {
	// return not implemented
	MsRtspMsg rsp;
	string data;
	rsp.m_reason = "Not Implemented";
	rsp.m_status = "501";
	rsp.m_version = msg.m_version;
	rsp.m_cseq = msg.m_cseq;
	rsp.m_session = msg.m_session;
	rsp.Dump(data);
	this->WriteBuffer((const uint8_t *)data.c_str(), data.size(), -1);
	return 0;
}

void MsRtspSink::OnStreamInfo(AVStream *video, int videoIdx, AVStream *audio, int audioIdx) {
	if (m_error || !video)
		return;

	MsMeidaSink::OnStreamInfo(video, videoIdx, audio, audioIdx);
	int buf_size = 2048;
	int ret;
	shared_ptr<MsEventHandler> handler;
	char sdp[2048];
	MsRtspMsg rsp;
	AVDictionary *opts = nullptr;

	auto source = MsResManager::GetInstance().GetMediaSource(m_streamID);
	if (!source) {
		MS_LOG_ERROR("source not found for streamID:%s", m_streamID.c_str());
		goto err;
	}

	m_reactor = dynamic_pointer_cast<MsReactor>(source);
	if (!m_reactor) {
		MS_LOG_ERROR("reactor not found for streamID:%s", m_streamID.c_str());
		goto err;
	}

	m_sock->SetNonBlock();
	handler = make_shared<MsRtspHandler>(m_reactor,
	                                     dynamic_pointer_cast<MsIRtspServer>(shared_from_this()));
	m_evt = std::make_shared<MsEvent>(m_sock, MS_FD_READ | MS_FD_CLOSE, handler);
	m_reactor->AddEvent(m_evt);

	m_pb = avio_alloc_context(
	    static_cast<unsigned char *>(av_malloc(buf_size)), buf_size, 1, this, nullptr,
	    [](void *opaque, const uint8_t *buf, int buf_size) -> int {
		    MsRtspSink *sink = static_cast<MsRtspSink *>(opaque);
		    return sink->WriteBuffer(buf, buf_size, 0);
	    },
	    nullptr);
	m_pb->max_packet_size = 1460;

	avformat_alloc_output_context2(&m_fmtCtx, nullptr, "rtp", nullptr);
	if (!m_fmtCtx || !m_pb) {
		MS_LOG_ERROR("Failed to allocate format context or IO context");
		goto err;
	}

	m_fmtCtx->pb = m_pb;
	m_fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

	m_outVideo = avformat_new_stream(m_fmtCtx, NULL);
	ret = avcodec_parameters_copy(m_outVideo->codecpar, m_video->codecpar);
	if (ret < 0) {
		MS_LOG_ERROR("Failed to copy codec parameters to output stream");
		goto err;
	}
	m_outVideo->codecpar->codec_tag = 0;

	av_dict_set(&opts, "rtpflags", "skip_rtcp", 0);
	ret = avformat_write_header(m_fmtCtx, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		MS_LOG_ERROR("Error occurred when writing header to HTTP sink");
		goto err;
	}

	if (m_audio) {
		m_pbAudio = avio_alloc_context(
		    static_cast<unsigned char *>(av_malloc(buf_size)), buf_size, 1, this, nullptr,
		    [](void *opaque, const uint8_t *buf, int buf_size) -> int {
			    MsRtspSink *sink = static_cast<MsRtspSink *>(opaque);
			    return sink->WriteBuffer(buf, buf_size, 2);
		    },
		    nullptr);
		m_pbAudio->max_packet_size = 1460;

		avformat_alloc_output_context2(&m_fmtCtxAudio, nullptr, "rtp", nullptr);
		if (!m_fmtCtxAudio || !m_pbAudio) {
			MS_LOG_ERROR("Failed to allocate format context or IO context for audio");
			goto err;
		}

		m_fmtCtxAudio->pb = m_pbAudio;
		m_fmtCtxAudio->flags |= AVFMT_FLAG_CUSTOM_IO;

		m_outAudio = avformat_new_stream(m_fmtCtxAudio, NULL);
		ret = avcodec_parameters_copy(m_outAudio->codecpar, m_audio->codecpar);
		if (ret < 0) {
			MS_LOG_ERROR("Failed to copy codec parameters to output audio stream");
			goto err;
		}
		m_outAudio->codecpar->codec_tag = 0;

		AVCodecParameters *codecpar = m_outAudio->codecpar;
		// if no extradata, generate it
		if (codecpar->extradata_size == 0 && codecpar->sample_rate > 0 &&
		    codecpar->ch_layout.nb_channels > 0 && codecpar->ch_layout.nb_channels < 3) {
			// generate aac extradata
			// Map sample rate to index
			int samplingFrequencyIndex = 15; // default to escape value
			const static int sampleRateTable[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
			                                      22050, 16000, 12000, 11025, 8000,  7350};
			for (int i = 0; i < 13; i++) {
				if (sampleRateTable[i] == codecpar->sample_rate) {
					samplingFrequencyIndex = i;
					break;
				}
			}

			// Get audio object type (profile + 1, AAC-LC = 2)
			int audioObjectType =
			    (codecpar->profile > 0) ? codecpar->profile + 1 : 2; // Default to AAC-LC
			int channelConfiguration = codecpar->ch_layout.nb_channels;

			// Build Audio Specific Config (2 bytes for AAC-LC)
			// Byte 0: audioObjectType (5 bits) | samplingFrequencyIndex (3 bits)
			// Byte 1: samplingFrequencyIndex (1 bit) | channelConfiguration (4 bits) |
			// frameLengthFlag (1 bit) | dependsOnCoreCoder (1 bit)
			uint16_t config = 0;
			config |= (audioObjectType & 0x1F) << 11;       // 5 bits
			config |= (samplingFrequencyIndex & 0x0F) << 7; // 4 bits
			config |= (channelConfiguration & 0x0F) << 3;   // 4 bits
			// frameLengthFlag = 0 (1024 samples per frame), dependsOnCoreCoder = 0

			uint8_t extradata[2];
			extradata[0] = (config >> 8) & 0xFF;
			extradata[1] = config & 0xFF;
			int extradata_size = 2;

			codecpar->extradata =
			    (uint8_t *)av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
			memcpy(codecpar->extradata, extradata, extradata_size);
			codecpar->extradata_size = extradata_size;
		}

		av_dict_set(&opts, "rtpflags", "skip_rtcp", 0);
		ret = avformat_write_header(m_fmtCtxAudio, &opts);
		av_dict_free(&opts);
		if (ret < 0) {
			MS_LOG_ERROR("Error occurred when writing header to rtsp sink for audio");
			goto err;
		}
	}

	{
		AVFormatContext *fmts[] = {m_fmtCtx, m_fmtCtxAudio};
		ret = av_sdp_create(fmts, m_fmtCtxAudio == nullptr ? 1 : 2, sdp, sizeof(sdp));
		if (ret < 0) {
			MS_LOG_ERROR("av_sdp_create failed");
			goto err;
		}
	}

	rsp.m_version = m_descb.m_version;
	rsp.m_status = "200";
	rsp.m_reason = "OK";
	rsp.m_cseq = m_descb.m_cseq;
	rsp.m_contentType.SetValue("application/sdp");
	rsp.m_contentBase.SetValue(m_descb.m_uri);
	rsp.SetBody(sdp, strlen(sdp));
	SendRtspMsg(rsp, m_sock.get());

	return;

err:
	m_error = true;
	this->DetachSourceNoLock();
	this->ReleaseResources();
}

void MsRtspSink::OnSourceClose() {
	m_error = true;
	this->ReleaseResources();
}

void MsRtspSink::OnStreamPacket(AVPacket *pkt) {
	if (!m_fmtCtx || !m_video || m_error) {
		return;
	}
	int ret;

	AVFormatContext *fmtCtx = pkt->stream_index == m_videoIdx ? m_fmtCtx : m_fmtCtxAudio;
	AVStream *outSt = pkt->stream_index == m_videoIdx ? m_outVideo : m_outAudio;
	AVStream *inSt = pkt->stream_index == m_videoIdx ? m_video : m_audio;
	int outIdx = pkt->stream_index == m_videoIdx ? m_outVideoIdx : m_outAudioIdx;
	int inIdx = pkt->stream_index;

	av_packet_rescale_ts(pkt, inSt->time_base, outSt->time_base);
	pkt->pos = -1;
	pkt->stream_index = outIdx;

	ret = av_write_frame(fmtCtx, pkt);
	if (ret < 0) {
		MS_LOG_ERROR("Error writing frame to rtsp sink, video: %d ret:%d",
		             pkt->stream_index == m_videoIdx, ret);
	}

	av_packet_rescale_ts(pkt, outSt->time_base, inSt->time_base);
	pkt->pos = -1;
	pkt->stream_index = inIdx;
}

void MsRtspSink::ActiveClose() {
	m_error = true;

	this->DetachSource();
	this->ReleaseResources();
}

void MsRtspSink::OnWriteEvent(shared_ptr<MsEvent> evt) {
	if (m_error)
		return;
	int fd = m_sock->GetFd();
	int ret = 0;

	std::lock_guard<std::mutex> lock(m_queDataMutex);
	while (m_queData.size()) {
		SData &sd = m_queData.front();
		uint8_t *pBuf = sd.m_buf;

		while (sd.m_len > 0) {
			ret = send(fd, pBuf, sd.m_len, 0);
			if (ret >= 0) {
				sd.m_len -= ret;
				pBuf += ret;
			} else if (MS_LAST_ERROR == EAGAIN) {
				if (pBuf != sd.m_buf) {
					memmove(sd.m_buf, pBuf, sd.m_len);
				}
				return;
			} else if (MS_LAST_ERROR == EINTR) {
				continue;
			} else {
				MS_LOG_INFO("http sink streamID:%s, sinkID:%d err:%d", m_streamID.c_str(), m_sinkID,
				            MS_LAST_ERROR);
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

	MS_LOG_INFO("rtsp sink streamID:%s, sinkID:%d all data sent", m_streamID.c_str(), m_sinkID);
}

void MsRtspSink::OnCloseEvent(shared_ptr<MsEvent> evt) { this->ActiveClose(); }

int MsRtspSink::WriteBuffer(const uint8_t *buf, int buf_size, int channel) {
	if (m_error)
		return -1;

	if (channel == -1) {
		if (m_playing) {
			if (m_queData.size() > 0) {
				SData sd;
				sd.m_buf = new uint8_t[buf_size];
				memcpy(sd.m_buf, buf, buf_size);
				sd.m_len = buf_size;

				std::lock_guard<std::mutex> lock(m_queDataMutex);
				m_queData.push(sd);
				return buf_size;
			} else {
				SendSmallBlock(buf, buf_size, m_sock->GetFd());
				return buf_size;
			}
		} else {
			SendSmallBlock(buf, buf_size, m_sock->GetFd());
			return buf_size;
		}
	}

	if (!m_playing || m_queData.size()) {
		SData sd;
		sd.m_buf = new uint8_t[buf_size + 4];
		uint8_t *pbuf = sd.m_buf;
		avio_w8(pbuf, '$');
		avio_w8(pbuf, channel);
		avio_wb16(pbuf, buf_size);
		memcpy(sd.m_buf + 4, buf, buf_size);
		sd.m_len = buf_size + 4;

		std::lock_guard<std::mutex> lock(m_queDataMutex);
		m_queData.push(sd);
		return buf_size;
	} else {
		int fd = m_sock->GetFd();
		uint8_t header_buf[4];
		uint8_t *pheader = header_buf;
		avio_w8(pheader, '$');
		avio_w8(pheader, channel);
		avio_wb16(pheader, buf_size);

		if (SendSmallBlock((const uint8_t *)header_buf, 4, fd) < 0) {
			MS_LOG_ERROR("rtsp sink send header failed");
			return buf_size;
		}

		const uint8_t *pBuf = buf;
		int pLen = buf_size;
		int ret = 0;

		while (pLen > 0) {
			ret = send(fd, pBuf, pLen, 0);
			if (ret >= 0) {
				pLen -= ret;
				pBuf += ret;
			} else if (MS_LAST_ERROR == EAGAIN) {
				SData sd;
				sd.m_buf = new uint8_t[pLen];
				memcpy(sd.m_buf, pBuf, pLen);
				sd.m_len = pLen;
				{
					std::lock_guard<std::mutex> lock(m_queDataMutex);
					m_queData.push(sd);
				}

				// regist write
				m_evt->SetEvent(MS_FD_READ | MS_FD_CLOSE | MS_FD_CONNECT);
				m_reactor->ModEvent(m_evt);

				MS_LOG_INFO("sink %s:%d regist write", m_type.c_str(), m_sinkID);

				return buf_size;
			} else if (MS_LAST_ERROR == EINTR) {
				continue;
			} else {
				MS_LOG_INFO("sink %s:%d err:%d", m_type.c_str(), m_sinkID, MS_LAST_ERROR);
				return buf_size;
			}
		}

		return buf_size;
	}
}

void MsRtspSink::ReleaseResources() {
	if (m_evt && m_reactor) {
		m_reactor->DelEvent(m_evt);
		m_evt = nullptr;
	}

	if (m_fmtCtx) {
		av_write_trailer(m_fmtCtx);
		if (m_fmtCtx->pb) {
			avio_flush(m_fmtCtx->pb);
			av_freep(&m_fmtCtx->pb->buffer);
			avio_context_free(&m_fmtCtx->pb);
		}
		avformat_free_context(m_fmtCtx);
		m_fmtCtx = nullptr;
	}

	if (m_fmtCtxAudio) {
		av_write_trailer(m_fmtCtxAudio);
		if (m_fmtCtxAudio->pb) {
			avio_flush(m_fmtCtxAudio->pb);
			av_freep(&m_fmtCtxAudio->pb->buffer);
			avio_context_free(&m_fmtCtxAudio->pb);
		}
		avformat_free_context(m_fmtCtxAudio);
		m_fmtCtxAudio = nullptr;
	}

	m_outVideo = nullptr;
	m_outAudio = nullptr;

	while (m_queData.size()) {
		SData &sd = m_queData.front();
		delete[] sd.m_buf;
		m_queData.pop();
	}
}
