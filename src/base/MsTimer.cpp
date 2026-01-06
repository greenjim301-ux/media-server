#include "MsTimer.h"
#include <vector>
#include "MsLog.h"
#include <thread>
#include <pthread.h>

unique_ptr<MsTimer> MsTimer::m_timerMgr;
mutex MsTimer::m_mutex;
condition_variable MsTimer::m_condiVar;

MsTimer::MsTimer()
	: m_timerID(0), m_exit(false)
{
}

MsTimer *MsTimer::Instance()
{
	if (MsTimer::m_timerMgr.get())
	{
		return MsTimer::m_timerMgr.get();
	}
	else
	{
		lock_guard<mutex> lk(MsTimer::m_mutex);

		if (MsTimer::m_timerMgr.get())
		{
			return MsTimer::m_timerMgr.get();
		}
		else
		{
			MsTimer::m_timerMgr = make_unique<MsTimer>();
			return MsTimer::m_timerMgr.get();
		}
	}
}

int MsTimer::AddTimer(const shared_ptr<MsReactor> &reactor,
					  MsMsg &msg, int inter, bool repeat)
{
	lock_guard<mutex> lk(MsTimer::m_mutex);

	if (++m_timerID <= 0)
	{
		m_timerID = 1;
	}

	shared_ptr<MsTimerItem> timer = make_shared<MsTimerItem>(
		reactor, m_timerID, msg, inter, repeat);
	m_timerItems.emplace(m_timerID, timer);

	// MS_LOG_DEBUG("add timer:%d", m_timerID);

	return m_timerID;
}

void MsTimer::DelTimer(int id)
{
	lock_guard<mutex> lk(MsTimer::m_mutex);

	this->DelTimer_i(id);
}

void MsTimer::ResetTimer(int id)
{
	if (id)
	{
		lock_guard<mutex> lk(MsTimer::m_mutex);

		auto it = m_timerItems.find(id);
		if (it != m_timerItems.end())
		{
			it->second->m_curTick = 0;
		}
	}
}

void MsTimer::Run()
{
	thread worker(&MsTimer::OnRun, this);
	worker.detach();
}

void MsTimer::OnRun()
{
	pthread_cond_t cond_;
	pthread_mutex_t mutex_;
	pthread_condattr_t cattr_;

	pthread_condattr_init(&cattr_);
	pthread_condattr_setclock(&cattr_, CLOCK_MONOTONIC);

	pthread_mutex_init(&mutex_, NULL);
	pthread_cond_init(&cond_, &cattr_);

	timespec end_at, tmp_at;
	clock_gettime(CLOCK_MONOTONIC, &end_at);

	pthread_mutex_lock(&mutex_);

	while (!m_exit)
	{
		end_at.tv_sec++;

		pthread_cond_timedwait(&cond_, &mutex_, &end_at);

		clock_gettime(CLOCK_MONOTONIC, &tmp_at);
		if (tmp_at.tv_sec - end_at.tv_sec > 1)
		{
			end_at = tmp_at;
			printf("timer change");
		}

		vector<int> timer;

		{
			lock_guard<mutex> lk(MsTimer::m_mutex);

			for (auto &it : m_timerItems)
			{
				if (it.second->Expire())
				{
					timer.emplace_back(it.second->m_timerID);
				}
			}

			for (auto &itExp : timer)
			{
				this->DelTimer_i(itExp);
			}
		}
	}

	pthread_mutex_unlock(&mutex_);

	pthread_condattr_destroy(&cattr_);
	pthread_cond_destroy(&cond_);
	pthread_mutex_destroy(&mutex_);
}

void MsTimer::Exit()
{
	m_exit = true;
}

void MsTimer::DelTimer_i(int id)
{
	if (id)
	{
		// MS_LOG_DEBUG("del timer:%d", id);
		m_timerItems.erase(id);
	}
}

MsTimer::MsTimerItem::MsTimerItem(const shared_ptr<MsReactor> &reactor,
								  int id, MsMsg &msg, int inter, bool repeat)
	: m_reactor(reactor), m_msg(msg), m_inter(inter), m_repeat(repeat), m_curTick(0), m_timerID(id)
{
}

MsTimer::MsTimerItem::~MsTimerItem()
{
}

bool MsTimer::MsTimerItem::Expire()
{
	if (++m_curTick == m_inter)
	{
		m_reactor->EnqueMsg(m_msg);

		if (m_repeat)
		{
			m_curTick = 0;
			return false;
		}
		else
		{
			return true;
		}
	}

	return false;
}
