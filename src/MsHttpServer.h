#pragma once
#include "MsHttpHandler.h"
#include "MsHttpMsg.h"
#include "MsOnvifHandler.h"
#include "MsReactor.h"
#include "MsSocket.h"
#include "nlohmann/json.hpp"
#include <map>
#include <memory>

using json = nlohmann::json;

class MsHttpServer : public MsIHttpServer {
public:
	MsHttpServer(int type, int id);

	void Run();
	void HandleMsg(MsMsg &msg);
	void HandleHttpReq(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);

private:
	void OnGenHttpRsp(MsMsg &msg);
	void OnNodeTimer();
	void ProbeOnvif(string &devid, int sid);

	void DeviceProcess(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void SetSysConfig(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void GetLiveUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void GetPlaybackUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void GetDevList(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void GetRegistDomain(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void QueryRecord(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void InitCatalog(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void AddDevice(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void DelDevice(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void GetGbServer(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void GetMediaNode(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void PtzControl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void NetMapConfig(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void QueryPreset(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void FileUpload(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void FileProcess(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);
	void FileUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len);

private:
	map<int, shared_ptr<MsEvent>> m_evts;
	map<string, shared_ptr<MsOnvifHandler>> m_onvif;

	int m_seqID = 0;
	int m_nodeId = 1;

	static map<int, shared_ptr<SMediaNode>> m_mediaNode;

	shared_ptr<SMediaNode> GetBestMediaNode(const string &devId, const string &bindIP);
};
