#pragma once

#include "MsCommon.h"
#include "sqlite/sqlite3.h"
#include <memory>
#include <mutex>

using namespace std;

class MsDbMgr {
public:
	MsDbMgr();
	~MsDbMgr();

	int Init();
	sqlite3 *GetSql();
	void RelSql();

	static MsDbMgr *Instance();

private:
	sqlite3 *m_sql;
	mutex m_sqlMutex;

	static unique_ptr<MsDbMgr> m_instance;
	static mutex m_mutex;
};
