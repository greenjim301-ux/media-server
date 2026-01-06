#pragma once
#include "MsReactor.h"
#include "MsDevMgr.h"

class MsOnvifHandler : public MsEventHandler
{
public:
	MsOnvifHandler(shared_ptr<MsReactor> r, shared_ptr<MsGbDevice> dev, int sid);
	~MsOnvifHandler();

	void HandleRead(shared_ptr<MsEvent> evt);
	void HandleClose(shared_ptr<MsEvent> evt);

	static void OnvifPtzControl(string user, string passwd, string url, 
		string profile, string presetID, int cmd, int ttout);
	static void QueryPreset(string user, string passwd,
		string url, string profile, int nseq);
public:
	enum {
		STAGE_S1,
		STAGE_S2,
		STAGE_S3,
		STAGE_S4,
	};

	void proc_s1(shared_ptr<MsEvent> evt);
	void proc_s2(shared_ptr<MsEvent> evt);
	void proc_s3(shared_ptr<MsEvent> evt);
	void proc_s4(shared_ptr<MsEvent> evt);

	void clear_evt(shared_ptr<MsEvent> evt);
	static int parse_uri(string& url, string& ip, int& port, string& uri);
	static void gen_digest(string& passwd, string& created, string& nonce, string& digest);

	shared_ptr<MsReactor> m_reactor;
	shared_ptr<MsGbDevice> m_dev;
	shared_ptr<MsEvent> m_evt;
	char* m_buf;
	int m_nrecv;
	int m_stage;
	int m_sid;
	string m_mediaurl;
	string m_ptzurl;
	string m_profile;
	string m_rtsp;
};