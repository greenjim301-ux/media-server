#include "MsLog.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>

#define MAX_LOG_LEN 2048
#define MAX_LOG_QUEUE 512

unique_ptr<MsLog> MsLog::m_log;
mutex MsLog::m_mutex;
condition_variable MsLog::m_condiVar;

MsLog::MsLog() : m_exit(false), m_level(LOG_LEVEL_INFO), m_curDay(-1), m_fp(NULL), m_days(3) {}

MsLog::~MsLog() {
	if (m_fp) {
		fclose(m_fp);
	}

	while (m_logQue.size()) {
		char *log = m_logQue.front();
		free(log);
		m_logQue.pop();
	}

	while (m_logBuf.size()) {
		char *log = m_logBuf.front();
		free(log);
		m_logBuf.pop();
	}
}

MsLog *MsLog::Instance() {
	if (MsLog::m_log.get()) {
		return MsLog::m_log.get();
	} else {
		lock_guard<mutex> lk(MsLog::m_mutex);

		if (MsLog::m_log.get()) {
			return MsLog::m_log.get();
		} else {
			MsLog::m_log = make_unique<MsLog>();
			return MsLog::m_log.get();
		}
	}
}

char *MsLog::GetLogBuf() {
	if (m_logBuf.size()) {
		char *buf = m_logBuf.front();
		m_logBuf.pop();
		return buf;
	} else {
		char *buf = (char *)malloc(MAX_LOG_LEN);
		return buf;
	}
}

void MsLog::RelLogBuf(char *buf) {
	if (m_logBuf.size() > MAX_LOG_QUEUE) {
		free(buf);
	} else {
		m_logBuf.emplace(buf);
	}
}

void MsLog::EnqueLog(char *log) { m_logQue.emplace(log); }

void MsLog::Log(int level, const char *strLog, ...) {
	if (level > m_level) {
		return;
	}

	unique_lock<mutex> lk(MsLog::m_mutex);

	char *logBuf = this->GetLogBuf();
	int maxLen = MAX_LOG_LEN - 1;

	va_list argList;

	va_start(argList, strLog);

	int nRet = vsnprintf(logBuf, maxLen, strLog, argList);

	va_end(argList);

	if (nRet < 0) {
		this->RelLogBuf(logBuf);
		lk.unlock();
	} else {
		if (nRet > maxLen - 1) {
			nRet = maxLen - 1;
		}

		logBuf[nRet] = '\n';
		logBuf[nRet + 1] = '\0';

		this->EnqueLog(logBuf);
		lk.unlock();

		MsLog::m_condiVar.notify_one();
	}
}

void MsLog::OnRun() {
	unique_lock<mutex> lk(MsLog::m_mutex);

	while (!m_exit) {
		MsLog::m_condiVar.wait(lk);

		while (m_logQue.size() && !m_exit) {
			char *log = m_logQue.front();
			m_logQue.pop();

			lk.unlock();

			time_t tt;
			struct tm *atm;

			time(&tt);
			atm = localtime(&tt);
			bool change = false;

			if (m_curDay != atm->tm_mday) {
				change = true;
				bool isAppend = (m_curDay == -1);
				m_curDay = atm->tm_mday;

				if (m_fp) {
					fclose(m_fp);
				}

				char fName[32];
				sprintf(fName, "log/log-%d.txt", m_curDay);

				if (isAppend) {
					m_fp = fopen(fName, "ab");
				} else {
					m_fp = fopen(fName, "wb");
				}

				string cmd = "ln -sf log-" + to_string(m_curDay) + ".txt log/log-current.txt";
				system(cmd.c_str());
			}

			fprintf(m_fp, "%04d-%02d-%02d %02d:%02d:%02d %s", atm->tm_year + 1900, atm->tm_mon + 1,
			        atm->tm_mday, atm->tm_hour, atm->tm_min, atm->tm_sec, log);
			fflush(m_fp);

			if (change) {
				tt -= m_days * 86400; // 4 dyas
				atm = localtime(&tt);

				char fName[32];
				sprintf(fName, "log/log-%d.txt", atm->tm_mday);

				unlink(fName);
			}

			lk.lock();

			this->RelLogBuf(log);
		}
	}

	lk.unlock();
}

void MsLog::Exit() {
	unique_lock<mutex> lk(MsLog::m_mutex);
	m_exit = true;
	lk.unlock();

	m_condiVar.notify_one();
}

void MsLog::SetLevel(int level) { m_level = level; }

void MsLog::downLog(char *&buf, int64_t &size) {
	time_t tt;
	struct tm *atm;

	time(&tt);
	atm = localtime(&tt);

	char fName[32];
	sprintf(fName, "log/log-%d.txt", atm->tm_mday);
	FILE *fp = fopen(fName, "rb");
	if (!fp) {
		size = 0;
		return;
	}

	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	if (size == 0)
		return;

	buf = (char *)malloc(size);
	fread(buf, size, 1, fp);
	fclose(fp);
}

void MsLog::Run() {
	thread worker(&MsLog::OnRun, this);
	worker.detach();
}