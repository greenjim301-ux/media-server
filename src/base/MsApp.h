#pragma once

#include <mutex>
#include <condition_variable>
#include <memory>
using namespace std;

class MsApp 
{
public:
	MsApp();

	static MsApp* Instance();

	void Run();
	void Exit();

private:
	bool m_exit;

	static unique_ptr<MsApp>  m_app;
	static mutex m_mutex;
	static condition_variable m_condiVar;
};

