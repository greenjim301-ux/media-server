#pragma once

#include "MsSocket.h"
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <list>

class MsPortAllocator
{
public:
	MsPortAllocator();

	shared_ptr<MsSocket> AllocPort(int type, string& ip, int& port);

	static MsPortAllocator* Instance();

private:
	int m_minPort;
	int m_maxPort;
	int m_curPort;

	static unique_ptr<MsPortAllocator>  m_instance;
	static mutex m_mutex;
	static condition_variable m_condiVar;
};

