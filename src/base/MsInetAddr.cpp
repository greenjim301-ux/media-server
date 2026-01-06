#include "MsInetAddr.h"

MsInetAddr::MsInetAddr(int af, const string& ip, int port)
	: m_af(af)
	, m_ip(ip)
	, m_port(port)
{

}

MsInetAddr::MsInetAddr()
	: m_af(AF_UNSPEC)
	, m_port(0)
{

}

int MsInetAddr::GetAF() const
{
	return m_af;
}

const char* MsInetAddr::GetIP() const
{
	return m_ip.c_str();
}

int MsInetAddr::GetPort() const
{
	return m_port;
}

void MsInetAddr::SetAF(int af)
{
	m_af = af;
}

void MsInetAddr::SetIP(const char* ip)
{
	m_ip = ip;
}

void MsInetAddr::SetPort(int port)
{
	m_port = port;
}

