#pragma once
#include "MsCommon.h"

class MsSocket;

class MsHttpMsg : public MsComMsg {
public:
	MsHttpMsg();

	void Dump(string &rsp);
	void Parse(char *&p2);

	MsComHeader m_connection;
	MsComHeader m_host;
	MsComHeader m_transport;
	MsComHeader m_access;
	MsComHeader m_access2;
	MsComHeader m_access3;
};

void SendHttpRsp(MsSocket *sock, const string &rspBody);
void SendHttpRsp(MsSocket *sock, MsHttpMsg &rsp);
void SendHttpRspEx(MsSocket *sock, const string &rspBody);
