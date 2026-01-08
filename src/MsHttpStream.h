#pragma once
#include "MsHttpHandler.h"
#include "MsHttpMsg.h"
#include "MsReactor.h"
#include <string>
#include <vector>

class MsHttpStream : public MsIHttpServer {
public:
	using MsIHttpServer::MsIHttpServer;

	void Run();
	void HandleHttpReq(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);

private:
	int m_seqID = 0;
};