#include "MsReactor.h"
#include "MsLog.h"
#include "MsTimer.h"

MsReactor::MsReactor(int type, int id) : m_index(0), m_type(type), m_id(id), m_exit(false) {
	m_efd = epoll_create(1);
	m_eventfd = eventfd(0, EFD_NONBLOCK);
}

MsReactor::~MsReactor() {
	MS_LOG_DEBUG("~reactor %d:%d", m_type, m_id);
	close(m_efd);
}

int MsReactor::AddEvent(shared_ptr<MsEvent> evt) {
	evt->GetEvent()->data.u32 = m_index;

	int ret = epoll_ctl(m_efd, EPOLL_CTL_ADD, evt->GetSocket()->GetFd(), evt->GetEvent());

	if (!ret) {
		m_events.emplace(m_index, evt);
		++m_index;
	}

	return ret;
}

int MsReactor::DelEvent(shared_ptr<MsEvent> evt) {
	if (!evt.get()) {
		return 0;
	}

	int index = evt->GetEvent()->data.u32;

	int ret = epoll_ctl(m_efd, EPOLL_CTL_DEL, evt->GetSocket()->GetFd(), evt->GetEvent());
	m_events.erase(index);

	return ret;
}

int MsReactor::ModEvent(shared_ptr<MsEvent> evt) {
	return epoll_ctl(m_efd, EPOLL_CTL_MOD, evt->GetSocket()->GetFd(), evt->GetEvent());
}

int MsReactor::AddTimer(MsMsg &msg, int inter, bool repeat /*= false*/) {
	return MsTimer::Instance()->AddTimer(shared_from_this(), msg, inter, repeat);
}

void MsReactor::DelTimer(int id) { MsTimer::Instance()->DelTimer(id); }

void MsReactor::ResetTimer(int id) { MsTimer::Instance()->ResetTimer(id); }

void MsReactor::Run() {
	MsReactorMgr::Instance()->Regist(shared_from_this());

	shared_ptr<MsSocket> eventfd = make_shared<MsSocket>(m_eventfd);
	shared_ptr<MsEventHandler> evtHandler = make_shared<MsNotifyHandler>(shared_from_this());
	shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(eventfd, MS_FD_READ, evtHandler);

	this->AddEvent(msEvent);
}

void MsReactor::Exit() {
	MsReactorMgr::Instance()->UnRegist(shared_from_this());

	m_events.clear();

	m_exit = true;
}

void MsReactor::HandleMsg(MsMsg &msg) {
	switch (msg.m_msgID) {
	case MS_EXIT:
		this->Exit();
		break;

	default:
		break;
	}
}

int MsReactor::Wait() {
	int ret;

	while (!m_exit) {
		ret = epoll_wait(m_efd, m_eventHandles, MS_MAX_EVENTS, -1);

		if (ret > 0) {
			for (int i = 0; i < ret; ++i) {
				auto it = m_events.find(m_eventHandles[i].data.u32);
				if (it != m_events.end()) {
					shared_ptr<MsEvent> pEvt = it->second;
					pEvt->HandleEvent(m_eventHandles[i].events);
				} else {
					MS_LOG_WARN("index:%d no event", m_eventHandles[i].data.u32);
				}
			}

			if (m_msgQue.size())
				this->ProcessMsgQue();
		} else if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				MS_LOG_ERROR("epoll_wait err:%d", errno);
				return -1;
			}
		} else if (m_msgQue.size()) {
			this->ProcessMsgQue();
		}
	}

	this->ProcessMsgQue();

	return 0;
}

void MsReactor::EnqueMsg(MsMsg &msg) {
	unique_lock<mutex> lk(m_mutex);

	m_msgQue.emplace(msg);

	lk.unlock();

	uint64_t c = 1;
	write(m_eventfd, &c, sizeof(c));
}

MsReactor::MsNotifyHandler::MsNotifyHandler(const shared_ptr<MsReactor> &reactor)
    : m_reactor(reactor) {}

MsReactor::MsNotifyHandler::~MsNotifyHandler() {}

void MsReactor::MsNotifyHandler::HandleRead(shared_ptr<MsEvent> evt) {
	MsSocket *sock = evt->GetSocket();
	uint64_t c;
	read(sock->GetFd(), &c, sizeof(c));

	m_reactor->ProcessMsgQue();
}

void MsReactor::ProcessMsgQue() {
	unique_lock<mutex> lk(m_mutex);
	vector<MsMsg> msgs;

	while (m_msgQue.size()) {
		msgs.emplace_back(m_msgQue.front());
		m_msgQue.pop();
	}

	lk.unlock();

	for (auto &msg : msgs) {
		this->HandleMsg(msg);
	}
}

int MsReactor::PostMsg(MsMsg &msg) {
	msg.m_srcType = m_type;
	msg.m_srcID = m_id;

	return MsReactorMgr::Instance()->PostMsg(msg);
}

void MsReactor::PostExit() {
	MsMsg msg;

	msg.m_msgID = MS_EXIT;

	this->EnqueMsg(msg);
}

unique_ptr<MsReactorMgr> MsReactorMgr::m_manager;
mutex MsReactorMgr::m_mutex;

MsReactorMgr::MsReactorMgr() {}

void MsReactorMgr::Regist(const shared_ptr<MsReactor> &reactor) {
	lock_guard<mutex> lk(MsReactorMgr::m_mutex);
	m_reactors[reactor->GetType()].emplace(reactor->GetID(), reactor);
}

void MsReactorMgr::UnRegist(const shared_ptr<MsReactor> &reactor) {
	lock_guard<mutex> lk(MsReactorMgr::m_mutex);

	auto it = m_reactors.find(reactor->GetType());

	if (it != m_reactors.end()) {
		it->second.erase(reactor->GetID());

		if (it->second.empty()) {
			m_reactors.erase(it);
		}
	}
}

int MsReactorMgr::PostMsg(MsMsg &msg) {
	lock_guard<mutex> lk(MsReactorMgr::m_mutex);

	auto it = m_reactors.find(msg.m_dstType);

	if (it == m_reactors.end()) {
		return -1;
	}

	map<int, shared_ptr<MsReactor>> &reactors = it->second;

	auto itr = reactors.find(msg.m_dstID);

	if (itr == reactors.end()) {
		return -2;
	} else {
		itr->second->EnqueMsg(msg);
	}

	return 0;
}

shared_ptr<MsReactor> MsReactorMgr::GetReactor(int type, int id) {
	lock_guard<mutex> lk(MsReactorMgr::m_mutex);

	auto it = m_reactors.find(type);

	if (it == m_reactors.end()) {
		return NULL;
	}

	map<int, shared_ptr<MsReactor>> &reactors = it->second;

	auto itr = reactors.find(id);

	if (itr == reactors.end()) {
		return NULL;
	} else {
		return itr->second;
	}
}

MsReactorMgr *MsReactorMgr::Instance() {
	if (MsReactorMgr::m_manager.get()) {
		return MsReactorMgr::m_manager.get();
	} else {
		lock_guard<mutex> lk(MsReactorMgr::m_mutex);

		if (MsReactorMgr::m_manager.get()) {
			return MsReactorMgr::m_manager.get();
		} else {
			MsReactorMgr::m_manager = make_unique<MsReactorMgr>();
			return MsReactorMgr::m_manager.get();
		}
	}
}
