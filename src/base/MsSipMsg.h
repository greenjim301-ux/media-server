#pragma once

#include "MsCommon.h"
#include "MsInetAddr.h"
#include <vector>

class MsSocket;

class MsSipContact : public MsComHeader
{
public:
	MsSipContact(const char* key) : MsComHeader(key) {}

	string GetID();
	string GetIP();
	int GetPort();
};

class MsSipCSeq : public MsComHeader
{
public:
	MsSipCSeq(const char* key) : MsComHeader(key) {}

	int GetCSeq();
	string GetMethond();
};

class MsSipFrom : public MsComHeader
{
public:
	MsSipFrom(const char* key) : MsComHeader(key) {}

	string GetID();
	string GetIP();
	int GetPort();
	bool HasTag();
	void AppendTag(string tag);
};

class MsSipVia : public MsComHeader
{
public:
	MsSipVia(const char* key) : MsComHeader(key){}

	string get_ip();
	int get_port();
	string GetTransport();
	string GetBranch();
	bool HasRport();
	void Rebuild(const string& transport, const string& branch, MsInetAddr& recvAddr);
};

class MsSipMsg : public MsComMsg
{
public:
	MsSipMsg();

	void Dump(string& rsp);
	void Parse(char*& p2);
	void CloneBasic(MsSipMsg& from);

	std::vector<MsSipVia>     m_vias;
	std::vector<MsComHeader> m_rrs;
	MsSipFrom    m_from;
	MsSipFrom    m_to;
	MsComHeader  m_callID;
	MsSipCSeq    m_cseq;
	MsSipContact m_contact;
	MsComIntVal  m_maxForwards;
	MsComHeader  m_userAgent;
	MsComIntVal  m_expires;
	MsComHeader  m_subject;
	MsComHeader  m_wwwAuthenticate;
	MsComAuth    m_authorization;
	MsComHeader  m_date;
	MsComHeader  m_event;
	MsComHeader  m_xSource;
};

string GenNonce();
void NgxGmtTime(time_t t, struct tm* tp);
string GetTimeStr();
bool AuthValid(MsComAuth& sipAuth, string& method, string pass);

void BuildIP(string& uri, const string& ip, int port);
void BuildUri(string& uri, const string& id, const string& ip, int port);
void BuildTo(MsSipFrom& from, const string& id, const string& ip, int port);
void BuildFrom(MsSipFrom& from, const string& id, const string& ip, int port);
void BuildVia(MsComHeader& via, const string& ip, int port);
void BuildCSeq(MsSipCSeq& cseq, int seq, const string& method);
void BuildContact(MsSipContact& contact, const string& id, const string& ip, int port);
void BuildSubject(MsComHeader& subject, const string& sender, const string& recver, bool live);

int SendSipMsg(MsSipMsg& msg, shared_ptr<MsSocket> s, string ip, int port);
int SendSipMsg(MsSipMsg& msg, shared_ptr<MsSocket> s, MsInetAddr& addr);
void BuildSipMsg(const string& fromIP, int fromPort, const string& fromID,
	const string& toIP, int toPort, const string& toID,
	int cseq, const string& method, MsSipMsg& sipMsg);

