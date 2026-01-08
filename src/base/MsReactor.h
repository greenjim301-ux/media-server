#pragma once
#include "MsEvent.h"
#include "MsMsg.h"
#include <map>
#include <mutex>
#include <queue>

class MsReactor : public enable_shared_from_this<MsReactor> {
public:
	MsReactor(int type, int id);
	virtual ~MsReactor();

	inline int GetType() { return m_type; }
	inline int GetID() { return m_id; }

	int AddEvent(shared_ptr<MsEvent> evt);
	int DelEvent(shared_ptr<MsEvent> evt);
	int ModEvent(shared_ptr<MsEvent> evt);

	int AddTimer(MsMsg &msg, int inter, bool repeat = false);
	void DelTimer(int id);
	void ResetTimer(int id);

	virtual void Run();
	virtual void Exit();
	virtual void HandleMsg(MsMsg &msg);

	int Wait();

	void EnqueMsg(MsMsg &msg);
	void ProcessMsgQue();

	int PostMsg(MsMsg &msg);
	void PostExit();
	inline bool IsExit() { return m_exit; }

private:
	int m_type;
	int m_id;
	bool m_exit;

	MS_EVENT m_eventHandles[MS_MAX_EVENTS];
	int m_index;

	map<int, shared_ptr<MsEvent>> m_events;
	int m_efd;
	int m_eventfd;

	class MsNotifyHandler : public MsEventHandler {
	public:
		MsNotifyHandler(const shared_ptr<MsReactor> &reactor);
		~MsNotifyHandler();

		void HandleRead(shared_ptr<MsEvent> evt);
		void HandleClose(shared_ptr<MsEvent> evt) {};

	private:
		shared_ptr<MsReactor> m_reactor;
	};

	queue<MsMsg> m_msgQue;

private:
	mutex m_mutex;
};

class MsReactorMgr {
public:
	MsReactorMgr();

	void Regist(const shared_ptr<MsReactor> &reactor);
	void UnRegist(const shared_ptr<MsReactor> &reactor);

	int PostMsg(MsMsg &msg);
	shared_ptr<MsReactor> GetReactor(int type, int id);

	static MsReactorMgr *Instance();

private:
	map<int, map<int, shared_ptr<MsReactor>>> m_reactors;

	static unique_ptr<MsReactorMgr> m_manager;
	static mutex m_mutex;
};