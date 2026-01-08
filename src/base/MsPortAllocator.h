#pragma once

#include "MsSocket.h"
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <vector>

class MsPortAllocator {
public:
	MsPortAllocator();

	shared_ptr<MsSocket> AllocPort(int type, string &ip, int &port);

	static MsPortAllocator *Instance();

private:
	int m_minPort;
	int m_maxPort;
	int m_curPort;

	static unique_ptr<MsPortAllocator> m_instance;
	static mutex m_mutex;
	static condition_variable m_condiVar;
};
