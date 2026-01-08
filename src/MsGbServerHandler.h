#pragma once
#include "MsEvent.h"
#include "MsInetAddr.h"
#include "MsReactor.h"
#include "MsSipMsg.h"
#include <memory>

class MsIGbServer : public MsReactor {
public:
	using MsReactor::MsReactor;
	virtual void HandleRegist(MsSipMsg &sipMsg, shared_ptr<MsSocket> s, MsInetAddr &addr) = 0;
	virtual void HandleMessage(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr,
	                           char *body, int len) = 0;
	virtual void HandleBye(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr) = 0;
	virtual void HandleCancel(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr) = 0;
	virtual void HandleNotify(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr,
	                          char *body, int len) = 0;
	virtual void HandleInvite(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr,
	                          char *body, int len) = 0;
	virtual void HandleResponse(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, char *body,
	                            int len) = 0;
	virtual void HandleAck(MsSipMsg &sipMsg) = 0;
};

class MsGbAcceptHandler : public MsEventHandler {
public:
	MsGbAcceptHandler(const shared_ptr<MsIGbServer> &server) : m_server(server) {}
	~MsGbAcceptHandler() = default;

	void HandleRead(shared_ptr<MsEvent> evt);
	void HandleClose(shared_ptr<MsEvent> evt);

private:
	shared_ptr<MsIGbServer> m_server;
};

class MsGbServerHandler : public MsEventHandler {
public:
	MsGbServerHandler(const shared_ptr<MsIGbServer> &server);
	~MsGbServerHandler();

	void HandleRead(shared_ptr<MsEvent> evt);
	void HandleClose(shared_ptr<MsEvent> evt);

private:
	shared_ptr<MsIGbServer> m_server;

	char *m_buf;
	int m_bufSize;
	int m_bufOff;
	MsInetAddr m_recvAddr;
};
