#pragma once
#include "MsReactor.h"
#include "MsHttpMsg.h"

class MsHttpClient :
	public MsReactor
{
public:
	MsHttpClient(string& url, const string& val, int type, int id, int msgID);

	void Run();
	void OnRsp(MsHttpMsg& msg, char* buf, int len);

public:
	string m_method;

private:
	void OnRun();

	int m_msgID;
	string   m_url;
	string   m_value;
};


