#pragma once

#include "MsCommon.h"

class MsSocket;

class MsRtspMsg : public MsComMsg {
public:
	MsRtspMsg();

	void Dump(string &rsp);
	void Parse(char *&p2);

	MsComIntVal m_cseq;
	MsComHeader m_contentBase;
	MsComHeader m_public;
	MsComHeader m_transport;
	MsComHeader m_session;
	MsComHeader m_range;
	MsComHeader m_rtpInfo;
	MsComHeader m_accept;
	MsComAuth m_wwwAuth;
	MsComHeader m_auth;
};

int SendRtspMsg(MsRtspMsg &msg, MsSocket *s);
