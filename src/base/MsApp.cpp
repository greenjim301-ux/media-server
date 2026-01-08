#include "MsApp.h"
#include "MsLog.h"

unique_ptr<MsApp> MsApp::m_app;
mutex MsApp::m_mutex;
condition_variable MsApp::m_condiVar;

MsApp::MsApp() : m_exit(false) {}

void MsApp::Run() {
	unique_lock<mutex> lk(MsApp::m_mutex);

	while (!m_exit) {
		m_condiVar.wait(lk);
	}

	lk.unlock();
}

void MsApp::Exit() {
	unique_lock<mutex> lk(MsApp::m_mutex);
	m_exit = true;
	lk.unlock();

	m_condiVar.notify_one();
}

MsApp *MsApp::Instance() {
	if (MsApp::m_app.get()) {
		return MsApp::m_app.get();
	} else {
		lock_guard<mutex> lk(MsApp::m_mutex);

		if (MsApp::m_app.get()) {
			return MsApp::m_app.get();
		} else {
			MsApp::m_app = make_unique<MsApp>();
			return MsApp::m_app.get();
		}
	}
}