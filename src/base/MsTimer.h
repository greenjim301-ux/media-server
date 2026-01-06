#pragma once

#include "MsOsConfig.h"
#include <string>
#include <memory>
#include "MsReactor.h"
#include <mutex>
#include "MsMsg.h"
#include <condition_variable>


class MsTimer
{
public:
	MsTimer();

	static MsTimer* Instance();

	int AddTimer(const shared_ptr<MsReactor>& reactor,
		MsMsg& msg, int inter, bool repeat);
	void DelTimer(int id);
	void ResetTimer(int id);

	void Run();
	void OnRun();
	void Exit();

private:
	class MsTimerItem
	{
	public:
		MsTimerItem(const shared_ptr<MsReactor>& reactor,
			int id, MsMsg& msg, int inter, bool repeat);

		~MsTimerItem();

		bool Expire();

		shared_ptr<MsReactor> m_reactor;
		MsMsg m_msg;
		int   m_inter;
		bool  m_repeat;
		int   m_curTick;
		int   m_timerID;
	};

	void DelTimer_i(int id);

	bool m_exit;
	map<int, shared_ptr<MsTimerItem>> m_timerItems;
	int m_timerID;

	static unique_ptr<MsTimer>  m_timerMgr;
	static mutex m_mutex;
	static condition_variable m_condiVar;
};

