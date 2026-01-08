#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>

#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

class MsConfig {
public:
	void LoadConfig();
	void WriteConfig();
	int GetConfigInt(const char *key);
	string GetConfigStr(const char *key);
	void SetConfigInt(const char *key, int val);
	void SetConfigStr(const char *key, const string &ss);
	json &GetConfigObj();

	static MsConfig *Instance();

private:
	json m_configJson;
	static unique_ptr<MsConfig> m_config;
	static mutex m_mutex;
};
