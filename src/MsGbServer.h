#pragma once
#include "MsDevMgr.h"
#include "MsGbServerHandler.h"
#include "MsLog.h"
#include "MsReactor.h"
#include "MsSipMsg.h"
#include "MsSocket.h"
#include "nlohmann/json.hpp"
#include "tinyxml2/tinyxml2.h"
#include <memory>
#include <set>

using json = nlohmann::json;
using namespace tinyxml2;

class MsGbServer : public MsIGbServer {
public:
	MsGbServer(int type, int id);
	~MsGbServer() = default;

	void Run();
	void HandleMsg(MsMsg &msg);

	void HandleRegist(MsSipMsg &sipMsg, shared_ptr<MsSocket> s, MsInetAddr &addr);
	void HandleMessage(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr, char *body,
	                   int len);
	void HandleKeepalive(const char *deviceID, MsSipMsg &rspMsg);
	void HandleCatalog(XMLElement *root, const char *deviceID);
	void HandleRecord(XMLElement *root, const char *deviceID);
	void HandleInvite(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr, char *body,
	                  int len);
	void HandleResponse(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, char *body, int len);
	void HandleInviteRsp(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, int status, char *body,
	                     int len);
	void HandleAck(MsSipMsg &sipMsg);
	void HandleBye(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr);
	void HandleCancel(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr);
	void HandleNotify(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr, char *body,
	                  int len);
	void HandleMediaStatus(XMLElement *root, MsSipMsg &msg);
	void HandlePreset(XMLElement *root, const char *deviceID);
	void GetRegistDomain(MsMsg &msg);

private:
	int GenCSeq();
	void InitCatalog(const string &serverID);
	void HttpInitCatalog(MsMsg &msg);
	void InitRecordInfo(MsMsg &msg);
	void ByeMsg(MsSipMsg &msg, shared_ptr<MsSocket> sock);

	void OnRegTimeout(string &id);
	void OnCataTimeout(int sn);
	void OnRecordTimeout(int sn);
	void OnInviteTimeout(string &val);
	void PtzControl(MsMsg &msg);
	void QueryPreset(MsMsg &msg);
	void QueryPresetTimeout(int sn);
	void InitInvite(MsMsg &msg);

private:
	string m_ip;
	string m_gbServerId;
	string m_pass;
	int m_port;
	int m_cseq;

	class RegistDomain {
	public:
		RegistDomain() : m_contact("Contact") {}

		MsSipContact m_contact;
		int m_timer;
		shared_ptr<MsSocket> m_sock;
		map<string, shared_ptr<MsGbDevice>> m_device;
	};

	void AddGbDevice(XMLElement *item, const char *domainID, shared_ptr<RegistDomain> dd);
	void DoPtz(shared_ptr<RegistDomain> domain, string devID, string ptzCmd, int timeout);
	void ClearDomain(shared_ptr<RegistDomain> domain);

	class GbSessionCtx {
	public:
		int m_sum = 0;
		int m_recevd = 0;
		int m_timer = 0;
		json m_record;
		MsMsg m_req;
		shared_ptr<RegistDomain> m_domain;
	};

	class InviteCtx {
	public:
		~InviteCtx() { MS_LOG_INFO("invite call:%s ctx destory", m_rsp.m_callID.m_value.c_str()); }

		shared_ptr<RegistDomain> m_domain;
		MsSipMsg m_req;
		MsSipMsg m_rsp;
		string m_dstIP;
		int m_dstPort;
		int m_timer;
		int m_srcType;
		int m_srcID;

		string m_startTime;
		string m_endTime;
	};

	map<string, shared_ptr<RegistDomain>> m_registDomain;
	map<int, shared_ptr<GbSessionCtx>> m_gbSessionCtx;
	map<string, shared_ptr<InviteCtx>> m_inviteCtx;
};
