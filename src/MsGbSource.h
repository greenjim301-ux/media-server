#pragma once
#include "MsCommon.h"
#include "MsResManager.h"
#include <condition_variable>
#include <mutex>
#include <thread>

class MsGbSource : public MsMediaSource {
public:
	MsGbSource(const std::string &streamID, SGbContext *ctx, int id)
	    : MsMediaSource(streamID, MS_GB_SOURCE, id), m_ctx(ctx), m_bufSize(64 * 1024),
	      m_bufReadOff(0), m_bufWriteOff(0) {
		m_buf = new uint8_t[m_bufSize];
	}

	~MsGbSource() {
		if (m_ctx) {
			delete m_ctx;
		}
		if (m_buf) {
			delete[] m_buf;
		}
	}

	void Work() override;
	void Exit() override;
	void HandleMsg(MsMsg &msg) override;
	void ProcessRtp(uint8_t *buf, int len);

	void ActiveClose() override {
		m_isClosing.store(true);
		m_condVar.notify_all();
		MsMediaSource::ActiveClose();
	}

	void OnSinksEmpty() override {
		m_isClosing.store(true);
		m_condVar.notify_all();
		MsMediaSource::OnSinksEmpty();
	}

private:
	void OnRun();
	void PsParseThread();
	int ReadBuffer(uint8_t *buf, int buf_size);
	void WriteBuffer(uint8_t *buf, int len);

private:
	bool m_firstPkt = true;
	uint16_t m_seq = 0;
	int m_payload = 96;

	uint8_t *m_buf;
	int m_bufSize;
	int m_bufReadOff;
	int m_bufWriteOff;

	std::mutex m_mutex;
	std::condition_variable m_condVar;
	std::unique_ptr<std::thread> m_psThread;

	SGbContext *m_ctx;
	shared_ptr<MsSocket> m_rtpSock; // for tcp active
};