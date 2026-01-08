#include "MsFileSource.h"
#include "MsCommon.h"
#include "MsLog.h"
#include "MsPortAllocator.h"
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
}

void MsFileSource::Work() {
	MsMediaSource::Work();

	std::thread worker([this]() { this->OnRun(); });
	worker.detach();
}

void MsFileSource::OnRun() {
	int ret;
	AVFormatContext *fmt_ctx = NULL;
	AVPacket *pkt = NULL;
	AVDictionary *options = NULL;

	av_dict_set(&options, "analyzeduration", "200000", 0);
	ret = avformat_open_input(&fmt_ctx, m_filename.c_str(), NULL, &options);
	av_dict_free(&options);

	if (ret < 0) {
		MS_LOG_ERROR("Could not open input url:%s, err:%d", m_filename.c_str(), ret);
		this->ActiveClose();
		return;
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0) {
		MS_LOG_ERROR("Could not find stream info url:%s, err:%d", m_filename.c_str(), ret);
		avformat_close_input(&fmt_ctx);
		this->ActiveClose();
		return;
	}

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (ret < 0) {
		MS_LOG_ERROR("Could not find video stream in url:%s, err:%d", m_filename.c_str(), ret);
		avformat_close_input(&fmt_ctx);
		this->ActiveClose();
		return;
	}

	m_videoIdx = ret;
	m_video = fmt_ctx->streams[m_videoIdx];
	if (m_video->codecpar->codec_id != AV_CODEC_ID_H264 &&
	    m_video->codecpar->codec_id != AV_CODEC_ID_H265) {
		MS_LOG_ERROR("not support codec:%d url:%s", m_video->codecpar->codec_id,
		             m_filename.c_str());
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
		if (pkt->stream_index == m_videoIdx) {
			int64_t pts = pkt->pts;
			if (pts == AV_NOPTS_VALUE) {
				pts = pkt->dts;
			}
			if (pts == AV_NOPTS_VALUE) {
				MS_LOG_ERROR("pts dts both AV_NOPTS_VALUE, url:%s", m_filename.c_str());
				av_packet_unref(pkt);
				continue;
			}

			pts = av_rescale_q(pts, m_video->time_base, {1, 1000});
			int64_t now = GetCurMs();

			if (m_lastPts == INT64_MAX) {
				m_lastPts = pts;
				m_lastMs = now;
			} else {
				int d = pts - m_lastPts;
				int64_t expect = m_lastMs + d;

				if (expect > now) {
					if (expect > now + 1000L) {
						MS_LOG_WARN("expect diff over 1000ms, time rebase");
						m_lastPts = INT64_MAX;
					} else {
						usleep((expect - now) * 1000);
						m_lastPts = pts;
						m_lastMs = expect;
					}
				}
			}

			this->NotifyStreamPacket(pkt);
		} else if (m_audioIdx >= 0 && pkt->stream_index == m_audioIdx) {
			this->NotifyStreamPacket(pkt);
		}
		av_packet_unref(pkt);
	}

	avformat_close_input(&fmt_ctx);
	av_packet_free(&pkt);
	this->ActiveClose();
}
