#pragma once
#include "MsOsConfig.h"
#include <string>

using namespace std;

class MsInetAddr
{
public:
	MsInetAddr();
	MsInetAddr(int af, const string& ip, int port);

	int GetAF() const;
	const char* GetIP() const;
	int GetPort() const;

	void SetAF(int af);
	void SetIP(const char* ip);
	void SetPort(int port);

private:
	int m_af;
	string m_ip;
	int m_port;
};

