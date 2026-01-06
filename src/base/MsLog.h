#pragma once
#include "MsOsConfig.h"

#include <memory>
#include <mutex>
#include <queue>
#include <condition_variable>

using namespace std;

#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3
#define LOG_LEVEL_VERBS 4

class MsLog
{
public:
	MsLog();
	~MsLog();

	static MsLog* Instance();

	void Log(int level, const char* strlog, ...);

	void Run();
	void OnRun();
	void Exit();
	void SetLevel(int level);
	void downLog(char*& buf, int64_t& size);

private:
	char* GetLogBuf();
	void RelLogBuf(char* buf);
	void EnqueLog(char* log);

	int m_days;
	int m_level;
	bool m_exit;
	queue<char*> m_logBuf;
	queue<char*> m_logQue;
	FILE* m_fp;
	int m_curDay;

	static unique_ptr<MsLog>  m_log;
	static mutex m_mutex;
	static condition_variable m_condiVar;
};

static const char* logLevel[] = { "ERROR","WARN","INFO","DEBUG", "VERBS" };

#define MS_LOG(LEVEL, FORMAT_STRING, ...) \
{\
    MsLog::Instance()->Log(LEVEL, "%s %s,%d: " FORMAT_STRING,  logLevel[LEVEL],  __FILE__,  __LINE__, ##__VA_ARGS__); \
}			

#define MS_LOG_INFO(FORMAT_STRING, ...) MS_LOG(LOG_LEVEL_INFO, FORMAT_STRING, ##__VA_ARGS__) 
#define MS_LOG_WARN(FORMAT_STRING, ...) MS_LOG(LOG_LEVEL_WARN, FORMAT_STRING, ##__VA_ARGS__)
#define MS_LOG_ERROR(FORMAT_STRING, ...) MS_LOG(LOG_LEVEL_ERROR, FORMAT_STRING, ##__VA_ARGS__)
#define MS_LOG_DEBUG(FORMAT_STRING, ...) MS_LOG(LOG_LEVEL_DEBUG, FORMAT_STRING, ##__VA_ARGS__)
#define MS_LOG_VERBS(FORMAT_STRING, ...) MS_LOG(LOG_LEVEL_VERBS, FORMAT_STRING, ##__VA_ARGS__)