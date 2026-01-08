#include "MsDbMgr.h"

#include "MsConfig.h"
#include "MsDbMgr.h"

unique_ptr<MsDbMgr> MsDbMgr::m_instance;
mutex MsDbMgr::m_mutex;

MsDbMgr::MsDbMgr() : m_sql(nullptr) {}

MsDbMgr::~MsDbMgr() {
	if (m_sql) {
		sqlite3_close(m_sql);
	}
}

int MsDbMgr::Init() {
	int rc = sqlite3_open("conf/media_server.db", &m_sql);
	if (rc) {
		printf("Can't open database: %s\n", sqlite3_errmsg(m_sql));
		return -1;
	}

	char *zErrMsg = 0;
	string sql;

	sql = "create table if not exists t_file (\
		file_id    INTEGER PRIMARY KEY,\
		name       TEXT NOT NULL,\
		size       INTEGER NOT NULL,\
		codec      TEXT NOT NULL,\
		res        TEXT NOT NULL,\
		duration   REAL DEFAULT 0,\
		frame_rate REAL DEFAULT 0\
		)";

	rc = sqlite3_exec(m_sql, sql.c_str(), NULL, 0, &zErrMsg);
	if (rc != SQLITE_OK) {
		printf("database error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		return 1;
	}

	sql = "create table if not exists device (\
		id INTEGER PRIMARY KEY ,\
		device_id TEXT NOT NULL UNIQUE,\
        parent_id TEXT NOT NULL,\
        domain_id TEXT NOT NULL,\
        name TEXT NOT NULL,\
        status TEXT NOT NULL,\
        manufacturer TEXT NOT NULL,\
        model TEXT NOT NULL,\
        owner TEXT NOT NULL,\
        civil_code TEXT NOT NULL,\
        address TEXT NOT NULL,\
        ip_addr TEXT NOT NULL,\
        user TEXT NOT NULL,\
        pass TEXT NOT NULL,\
        longitude  TEXT NOT NULL,\
        latitude  TEXT NOT NULL,\
        url TEXT NOT NULL,\
        ptz_type INTEGER NOT NULL,\
        type INTEGER NOT NULL,\
        protocol INTEGER NOT NULL,\
        port INTEGER NOT NULL,\
        bind_ip  TEXT NOT NULL,\
        remark  TEXT NOT NULL,\
		codec TEXT DEFAULT 'unknown',\
		resolution TEXT DEFAULT 'unknown',\
		onvif_profile  TEXT DEFAULT '',\
		onvif_ptz_url  TEXT DEFAULT '' \
		)";

	rc = sqlite3_exec(m_sql, sql.c_str(), NULL, 0, &zErrMsg);
	if (rc != SQLITE_OK) {
		printf("database error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		return 1;
	}

	sql = "create table if not exists group_dev  (\
		 group_id  TEXT NOT NULL,\
		 device_id  TEXT NOT NULL,\
         name  TEXT NOT NULL,\
         type  INTEGER NOT NULL,\
		PRIMARY KEY( group_id ,  device_id ) \
		) ";

	rc = sqlite3_exec(m_sql, sql.c_str(), NULL, 0, &zErrMsg);
	if (rc != SQLITE_OK) {
		printf("database error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		return 1;
	}

	return 0;
}

sqlite3 *MsDbMgr::GetSql() {
	m_sqlMutex.lock();
	return m_sql;
}

void MsDbMgr::RelSql() { m_sqlMutex.unlock(); }

MsDbMgr *MsDbMgr::Instance() {
	if (MsDbMgr::m_instance.get()) {
		return MsDbMgr::m_instance.get();
	} else {
		lock_guard<mutex> lk(MsDbMgr::m_mutex);

		if (MsDbMgr::m_instance.get()) {
			return MsDbMgr::m_instance.get();
		} else {
			MsDbMgr::m_instance = make_unique<MsDbMgr>();
			return MsDbMgr::m_instance.get();
		}
	}
}
