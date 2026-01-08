#include "MsConfig.h"
#include "MsLog.h"
#include <fstream>
#include <iomanip>

unique_ptr<MsConfig> MsConfig::m_config;
mutex MsConfig::m_mutex;

void MsConfig::LoadConfig() {
	ifstream fs("conf/config.json");
	fs >> m_configJson;
}

void MsConfig::WriteConfig() {
	ofstream fs("conf/config.json");
	fs << std::setw(4) << m_configJson;
}

int MsConfig::GetConfigInt(const char *key) {
	if (m_configJson.count(key)) {
		return m_configJson[key];
	} else {
		return 0;
	}
}

string MsConfig::GetConfigStr(const char *key) {
	if (m_configJson.count(key)) {
		return m_configJson[key];
	} else {
		return string();
	}
}

void MsConfig::SetConfigInt(const char *key, int val) {
	if (!m_configJson[key].is_null()) {
		int v = m_configJson[key];
		if (v == val) {
			return;
		}
	}

	m_configJson[key] = val;

	ofstream fs("conf/config.json");
	fs << std::setw(4) << m_configJson;
}

void MsConfig::SetConfigStr(const char *key, const string &val) {
	if (!m_configJson[key].is_null()) {
		string v = m_configJson[key];
		if (v == val) {
			return;
		}
	}

	m_configJson[key] = val;

	ofstream fs("conf/config.json");
	fs << std::setw(4) << m_configJson;
}

MsConfig *MsConfig::Instance() {
	if (MsConfig::m_config.get()) {
		return MsConfig::m_config.get();
	} else {
		lock_guard<mutex> lk(MsConfig::m_mutex);

		if (MsConfig::m_config.get()) {
			return MsConfig::m_config.get();
		} else {
			MsConfig::m_config = make_unique<MsConfig>();
			return MsConfig::m_config.get();
		}
	}
}

json &MsConfig::GetConfigObj() { return m_configJson; }
