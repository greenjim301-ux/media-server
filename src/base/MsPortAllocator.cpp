#include "MsPortAllocator.h"
#include "MsDevMgr.h"
#include "MsLog.h"
#include "MsConfig.h"

unique_ptr<MsPortAllocator> MsPortAllocator::m_instance;
mutex MsPortAllocator::m_mutex;
condition_variable MsPortAllocator::m_condiVar;

MsPortAllocator::MsPortAllocator()
{
	m_minPort = MsConfig::Instance()->GetConfigInt("minPort");
	m_maxPort = MsConfig::Instance()->GetConfigInt("maxPort");

	if (m_minPort == 0)
		m_minPort = 10000;
	if (m_maxPort == 0)
		m_maxPort = 20000;

	m_curPort = m_minPort;
}

shared_ptr<MsSocket> MsPortAllocator::AllocPort(int type, string &ip, int &port)
{
	bool bindPort;
	int nn = 1000;
	lock_guard<mutex> lk(MsPortAllocator::m_mutex);

	while (nn--)
	{
		shared_ptr<MsSocket> s = make_shared<MsSocket>(AF_INET, type, 0);
		MsInetAddr addr(AF_INET, ip, m_curPort);

		if (s->Bind(addr))
		{
			bindPort = false;
		}
		else
		{
			port = m_curPort;
			bindPort = true;
		}

		m_curPort += 2;

		if (m_curPort > m_maxPort)
		{
			m_curPort = m_minPort;
		}

		if (bindPort)
		{
			return s;
		}
	}

	return NULL;
}

MsPortAllocator *MsPortAllocator::Instance()
{
	if (MsPortAllocator::m_instance.get())
	{
		return MsPortAllocator::m_instance.get();
	}
	else
	{
		lock_guard<mutex> lk(MsPortAllocator::m_mutex);

		if (MsPortAllocator::m_instance.get())
		{
			return MsPortAllocator::m_instance.get();
		}
		else
		{
			MsPortAllocator::m_instance = make_unique<MsPortAllocator>();
			return MsPortAllocator::m_instance.get();
		}
	}
}
