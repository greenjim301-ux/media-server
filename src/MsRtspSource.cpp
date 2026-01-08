#include "MsRtspSource.h"
#include "MsLog.h"
#include <thread>

void MsRtspSource::Work() {
	MsMediaSource::Work();

	std::thread worker([this]() { this->OnRun(); });
	worker.detach();
}

void MsRtspSource::OnRun() {
	int ret;
	AVFormatContext *fmt_ctx = NULL;
	AVPacket *pkt = NULL;
	AVDictionary *options = NULL;

	// Add rtsp_transport=tcp option if URL is RTSP
	if (m_url.find("rtsp://") == 0 || m_url.find("RTSP://") == 0) {
		av_dict_set(&options, "rtsp_transport", "tcp", 0);
	}

	av_dict_set(&options, "analyzeduration", "200000", 0);
	ret = avformat_open_input(&fmt_ctx, m_url.c_str(), NULL, &options);
	av_dict_free(&options);

	if (ret < 0) {
		MS_LOG_ERROR("Could not open input url:%s, err:%d", m_url.c_str(), ret);
		this->ActiveClose();
		return;
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0) {
		MS_LOG_ERROR("Could not find stream info url:%s, err:%d", m_url.c_str(), ret);
		avformat_close_input(&fmt_ctx);
		this->ActiveClose();
		return;
	}

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (ret < 0) {
		MS_LOG_ERROR("Could not find video stream in url:%s, err:%d", m_url.c_str(), ret);
		avformat_close_input(&fmt_ctx);
		this->ActiveClose();
		return;
	}

	m_videoIdx = ret;
	m_video = fmt_ctx->streams[m_videoIdx];
	if (m_video->codecpar->codec_id != AV_CODEC_ID_H264 &&
	    m_video->codecpar->codec_id != AV_CODEC_ID_H265) {
		MS_LOG_ERROR("not support codec:%d url:%s", m_video->codecpar->codec_id, m_url.c_str());
		avformat_close_input(&fmt_ctx);
		this->ActiveClose();
		return;
	}

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (ret >= 0) {
		if (fmt_ctx->streams[ret]->codecpar->codec_id == AV_CODEC_ID_AAC) {
			m_audioIdx = ret;
			m_audio = fmt_ctx->streams[m_audioIdx];
		}
	}

	this->NotifyStreamInfo();

	pkt = av_packet_alloc();

	/* read frames from the file */
	while (av_read_frame(fmt_ctx, pkt) >= 0 && !m_isClosing.load()) {
		if (pkt->stream_index == m_videoIdx || pkt->stream_index == m_audioIdx) {
			this->NotifyStreamPacket(pkt);
		}
		av_packet_unref(pkt);
	}

	avformat_close_input(&fmt_ctx);
	av_packet_free(&pkt);
	this->ActiveClose();
}
