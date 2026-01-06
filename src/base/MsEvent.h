#pragma once
#include "MsSocket.h"
#include <memory>

class MsEvent;

class MsEventHandler : public enable_shared_from_this<MsEventHandler>
{
public:
	virtual void HandleRead(shared_ptr<MsEvent> evt) = 0;
	virtual void HandleClose(shared_ptr<MsEvent> evt) = 0;
	virtual void HandleWrite(shared_ptr<MsEvent> evt) {}
};

class MsEvent : public enable_shared_from_this<MsEvent>
{
public:
	MsEvent(shared_ptr<MsSocket>& sock, int eventMask, 
		const shared_ptr<MsEventHandler>& handler);
	
	~MsEvent();

	MS_EVENT* GetEvent();
	void SetEvent(int eventMask);
	void SetHandler(shared_ptr<MsEventHandler> handler);
	MsSocket* GetSocket();
	shared_ptr<MsSocket> GetSharedSocket();
    shared_ptr<MsEventHandler> GetHandler(){return m_handler;}
	void HandleEvent(uint32_t eventMask);

private:
	MS_EVENT m_event;
	int m_eventMask;
	shared_ptr<MsSocket> m_sock;
	shared_ptr<MsEventHandler> m_handler;
};



