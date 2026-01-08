#include "MsGbServer.h"
#include "MsConfig.h"
#include "MsDevMgr.h"
#include "MsGbServerHandler.h"
#include "MsPortAllocator.h"
#include <thread>

MsGbServer::MsGbServer(int type, int id) : MsIGbServer(type, id), m_cseq(0) {}

void MsGbServer::Run() {
	MsIGbServer::Run();

	MsConfig *config = MsConfig::Instance();
	m_ip = config->GetConfigStr("localBindIP");
	m_port = config->GetConfigInt("gbServerPort");
	m_gbServerId = config->GetConfigStr("gbServerID");
	m_pass = config->GetConfigStr("gbServerPass");

	// udp bind
	auto sock = make_shared<MsSocket>(AF_INET, SOCK_DGRAM, 0);
	MsInetAddr bindAddr(AF_INET, m_ip, m_port);
	if (0 != sock->Bind(bindAddr)) {
		MS_LOG_ERROR("gb server bind:%s:%d err:%d", m_ip.c_str(), m_port, MS_LAST_ERROR);
		this->Exit();
		return;
	}
	MS_LOG_INFO("gb server bind:%s:%d", m_ip.c_str(), m_port);

	shared_ptr<MsEventHandler> h =
	    make_shared<MsGbServerHandler>(dynamic_pointer_cast<MsIGbServer>(shared_from_this()));
	shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(sock, MS_FD_READ, h);
	this->AddEvent(msEvent);

	// add tcp listen socket
	shared_ptr<MsSocket> tcpSock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);
	MsInetAddr tcpBindAddr(AF_INET, "0.0.0.0", m_port);
	if (0 != tcpSock->Bind(tcpBindAddr)) {
		MS_LOG_ERROR("gb server tcp bind:%d err:%d", m_port, MS_LAST_ERROR);
		this->Exit();
		return;
	}
	if (0 != tcpSock->Listen()) {
		MS_LOG_ERROR("gb server tcp listen:%d err:%d", m_port, MS_LAST_ERROR);
		this->Exit();
		return;
	}
	shared_ptr<MsEventHandler> tcpH =
	    make_shared<MsGbAcceptHandler>(dynamic_pointer_cast<MsIGbServer>(shared_from_this()));
	shared_ptr<MsEvent> tcpEvent = make_shared<MsEvent>(tcpSock, MS_FD_ACCEPT, tcpH);
	this->AddEvent(tcpEvent);

	thread worker(&MsIGbServer::Wait, shared_from_this());
	worker.detach();
}

void MsGbServer::HandleMsg(MsMsg &msg) {
	switch (msg.m_msgID) {
	case MS_REG_TIME_OUT:
		this->OnRegTimeout(msg.m_strVal);
		break;

	case MS_INIT_CATALOG:
		this->InitCatalog(msg.m_strVal);
		break;

	case MS_CATALOG_TIME_OUT:
		this->OnCataTimeout(msg.m_intVal);
		break;

	case MS_INVITE_TIME_OUT:
		this->OnInviteTimeout(msg.m_strVal);
		break;

	case MS_HTTP_INIT_CATALOG:
		this->HttpInitCatalog(msg);
		break;

	case MS_PTZ_CONTROL:
		this->PtzControl(msg);
		break;

	case MS_INIT_RECORD:
		this->InitRecordInfo(msg);
		break;

	case MS_RECORD_TIME_OUT:
		this->OnRecordTimeout(msg.m_intVal);
		break;

	case MS_QUERY_PRESET:
		this->QueryPreset(msg);
		break;

	case MS_QUERY_PRESET_TIMEOUT:
		this->QueryPresetTimeout(msg.m_intVal);
		break;

	case MS_GB_SERVER_HANDLER_CLOSE: {
		int fd = msg.m_intVal;
		for (auto it = m_registDomain.begin(); it != m_registDomain.end();) {
			shared_ptr<RegistDomain> domain = it->second;
			if (domain->m_sock->GetFd() == fd) {
				MS_LOG_INFO("gb server handler close, clear domain:%s", it->first.c_str());
				this->ClearDomain(domain);
				it = m_registDomain.erase(it);
			} else {
				++it;
			}
		}
	} break;

	case MS_STOP_INVITE_CALL: {
		string callID = msg.m_strVal;
		shared_ptr<InviteCtx> ctx;
		if (callID.size()) {
			auto it = m_inviteCtx.find(callID);
			if (it != m_inviteCtx.end()) {
				MS_LOG_INFO("stop invite call:%s", callID.c_str());
				ctx = it->second;
			}
		} else {
			int srcType = msg.m_srcType;
			int srcID = msg.m_srcID;
			for (auto it = m_inviteCtx.begin(); it != m_inviteCtx.end(); ++it) {
				shared_ptr<InviteCtx> &x = it->second;
				if (x->m_srcType == srcType && x->m_srcID == srcID) {
					MS_LOG_INFO("stop invite call:%s by src %d:%d", it->first.c_str(), srcType,
					            srcID);
					ctx = x;
					break;
				}
			}
		}
		if (ctx) {
			this->ByeMsg(ctx->m_rsp, ctx->m_domain->m_sock);
			m_inviteCtx.erase(ctx->m_req.m_callID.m_value);
		}
	} break;

	case MS_GET_REGIST_DOMAIN:
		this->GetRegistDomain(msg);
		break;

	case MS_INIT_INVITE:
		this->InitInvite(msg);
		break;

	default:
		MsIGbServer::HandleMsg(msg);
		break;
	}
}

void MsGbServer::HandleRegist(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr) {
	MsSipMsg rspMsg;
	rspMsg.CloneBasic(sipMsg);
	rspMsg.m_to.AppendTag(GenRandStr(16));
	string domainID = sipMsg.m_contact.GetID();

	if (sipMsg.m_authorization.m_exist) {
		if (AuthValid(sipMsg.m_authorization, sipMsg.m_method, m_pass)) {
			rspMsg.m_status = "200";
			rspMsg.m_reason = "OK";
			rspMsg.m_contact = sipMsg.m_contact;
			rspMsg.m_date.SetValue(GetTimeStr());

			int exp = sipMsg.m_expires.GetIntVal();

			if (!sock->IsTcp() &&
			    (!sipMsg.m_contact.GetPort() || MsConfig::Instance()->GetConfigInt("useRAddr"))) {
				BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), addr.GetIP(),
				             addr.GetPort());
			}

			MS_LOG_INFO("reg:%s exp:%d ip:%s:%d", domainID.c_str(), exp,
			            sipMsg.m_contact.GetIP().c_str(), sipMsg.m_contact.GetPort());

			auto it = m_registDomain.find(domainID);
			if (it == m_registDomain.end()) {
				if (exp <= 0) {
					MS_LOG_WARN("reg:%s already removed", domainID.c_str());
				} else {
					shared_ptr<RegistDomain> domain = make_shared<RegistDomain>();
					MsMsg msg;
					msg.m_msgID = MS_REG_TIME_OUT;
					msg.m_strVal = domainID;
					domain->m_timer = this->AddTimer(msg, exp + 30);

					string fromIP = sipMsg.m_from.GetIP();
					int fromPort = sipMsg.m_from.GetPort();
					string conIP = sipMsg.m_contact.GetIP();

					if (fromIP.size() && fromPort && fromIP != conIP) {
						MS_LOG_WARN("contact ip diff domain ip:%s contact ip:%s", fromIP.c_str(),
						            conIP.c_str());
						// BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), fromIP,
						// fromPort);
					}

					string mapIP = MsDevMgr::Instance()->GetMapIP(sipMsg.m_contact.GetIP());
					if (mapIP.size()) {
						MS_LOG_INFO("map ip %s->%s", sipMsg.m_contact.GetIP().c_str(),
						            mapIP.c_str());
						if (fromPort != 0) {
							BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), mapIP,
							             fromPort);
						} else {
							BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), mapIP,
							             sipMsg.m_contact.GetPort());
						}
					}

					domain->m_contact = sipMsg.m_contact;
					domain->m_sock = sock;
					m_registDomain.emplace(domainID, domain);

					MsDevMgr::Instance()->GetDomainDevice(domainID, domain->m_device);

					MsMsg catMsg;
					catMsg.m_msgID = MS_INIT_CATALOG;
					catMsg.m_strVal = domainID;
					this->AddTimer(catMsg, 1);
				}
			} else {
				shared_ptr<RegistDomain> &domain = it->second;
				this->DelTimer(domain->m_timer);

				if (exp <= 0) {
					MS_LOG_DEBUG("domain:%s unregist", domainID.c_str());
					this->ClearDomain(domain);
					m_registDomain.erase(it);
				} else {
					MsMsg msg;
					msg.m_msgID = MS_REG_TIME_OUT;
					msg.m_strVal = domainID;
					domain->m_timer = this->AddTimer(msg, exp + 30);

					string fromIP = sipMsg.m_from.GetIP();
					int fromPort = sipMsg.m_from.GetPort();
					string conIP = sipMsg.m_contact.GetIP();

					if (fromIP.size() && fromPort && fromIP != conIP) {
						MS_LOG_WARN("contact ip diff domain ip:%s contact ip:%s", fromIP.c_str(),
						            conIP.c_str());
						// BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), fromIP,
						// fromPort);
					}

					string mapIP = MsDevMgr::Instance()->GetMapIP(sipMsg.m_contact.GetIP());
					if (mapIP.size()) {
						MS_LOG_INFO("map ip %s->%s", sipMsg.m_contact.GetIP().c_str(),
						            mapIP.c_str());
						if (fromPort != 0) {
							BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), mapIP,
							             fromPort);
						} else {
							BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), mapIP,
							             sipMsg.m_contact.GetPort());
						}
					}

					domain->m_contact = sipMsg.m_contact;
					domain->m_sock = sock;

					MsMsg catMsg;
					catMsg.m_msgID = MS_INIT_CATALOG;
					catMsg.m_strVal = domainID;
					this->AddTimer(catMsg, 1);
				}
			}
		} else {
			MS_LOG_WARN("reg:%s auth failed", domainID.c_str());
			rspMsg.m_status = "403";
			rspMsg.m_reason = "Forbidden";
		}
	} else {
		MS_LOG_INFO("reg:%s no auth", domainID.c_str());

		rspMsg.m_status = "401";
		rspMsg.m_reason = "Unauthorized";

		string &strAuth = rspMsg.m_wwwAuthenticate.m_value;
		strAuth += "Digest realm=\"";
		strAuth += m_gbServerId;
		strAuth += "\", nonce=\"";
		strAuth += GenNonce();
		strAuth += "\", algorithm=MD5";
		rspMsg.m_wwwAuthenticate.m_exist = true;
	}

	SendSipMsg(rspMsg, sock, addr);
}

void MsGbServer::HandleMessage(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr,
                               char *body, int len) {
	MsSipMsg rspMsg;
	rspMsg.CloneBasic(sipMsg);
	rspMsg.m_status = "200";
	rspMsg.m_reason = "OK";

	if (!rspMsg.m_to.HasTag()) {
		rspMsg.m_to.AppendTag(GenRandStr(16));
	}

	if (!len) {
		SendSipMsg(rspMsg, sock, addr);
		return;
	}

	if (!strcasecmp(sipMsg.m_contentType.m_value.c_str(), "Application/MANSCDP+xml")) {
		tinyxml2::XMLDocument doc;
		int ret = doc.Parse(body, len);
		if (ret) {
			MS_LOG_WARN("parse xml err:%d", ret);
			SendSipMsg(rspMsg, sock, addr);
			return;
		}

		XMLElement *root = doc.RootElement();
		XMLElement *cmd = root->FirstChildElement("CmdType");
		XMLElement *domainID = root->FirstChildElement("DeviceID");
		const char *strCmd = cmd->GetText();

		if (!strcmp("Keepalive", strCmd)) {
			this->HandleKeepalive(domainID->GetText(), rspMsg);
		} else if (!strcmp("Catalog", strCmd)) {
			this->HandleCatalog(root, domainID->GetText());
		} else if (!strcmp("RecordInfo", strCmd)) {
			this->HandleRecord(root, domainID->GetText());
		} else if (!strcmp("MediaStatus", strCmd)) {
			// first send 200 OK, then send bye
			SendSipMsg(rspMsg, sock, addr);
			this->HandleMediaStatus(root, sipMsg);
			return;
		} else if (!strcmp("PresetQuery", strCmd)) {
			this->HandlePreset(root, domainID->GetText());
		} else {
			MS_LOG_WARN("unknown cmd:%s %s %s", root->Name(), cmd->GetText(), domainID->GetText());
		}
	}

	SendSipMsg(rspMsg, sock, addr);
}

void MsGbServer::HandleKeepalive(const char *domainID, MsSipMsg &rspMsg) {
	if (domainID) {
		if (!m_registDomain.count(domainID)) {
			rspMsg.m_status = "403";
			rspMsg.m_reason = "Forbidden";
		} else {
			MS_LOG_INFO("keepalive:%s", domainID);
		}
	}
}

void MsGbServer::AddGbDevice(XMLElement *item, const char *domainID, shared_ptr<RegistDomain> dd) {
	shared_ptr<MsGbDevice> device = make_shared<MsGbDevice>(GB_DEV);

	device->m_deviceID = item->FirstChildElement("DeviceID")->GetText();
	device->m_domainID = domainID;

	if (item->FirstChildElement("Name"))
		if (item->FirstChildElement("Name")->GetText())
			GbkToUtf8(device->m_name, item->FirstChildElement("Name")->GetText());

	if (device->m_name.size() == 0) {
		device->m_name = device->m_deviceID;
	}

	if (item->FirstChildElement("Status"))
		if (item->FirstChildElement("Status")->GetText())
			device->m_status = item->FirstChildElement("Status")->GetText();

	if (item->FirstChildElement("Manufacturer")) {
		GbkToUtf8(device->m_manufacturer, item->FirstChildElement("Manufacturer")->GetText());
	}

	if (item->FirstChildElement("Model")) {
		GbkToUtf8(device->m_model, item->FirstChildElement("Model")->GetText());
	}

	if (item->FirstChildElement("Owner")) {
		GbkToUtf8(device->m_owner, item->FirstChildElement("Owner")->GetText());
	}

	if (item->FirstChildElement("Address")) {
		GbkToUtf8(device->m_address, item->FirstChildElement("Address")->GetText());
	}

	if (item->FirstChildElement("CivilCode"))
		if (item->FirstChildElement("CivilCode")->GetText())
			device->m_civilCode = item->FirstChildElement("CivilCode")->GetText();

	if (item->FirstChildElement("IPAddress"))
		if (item->FirstChildElement("IPAddress")->GetText())
			device->m_ipaddr = item->FirstChildElement("IPAddress")->GetText();

	if (item->FirstChildElement("Longitude"))
		if (item->FirstChildElement("Longitude")->GetText())
			device->m_longitude = item->FirstChildElement("Longitude")->GetText();

	if (item->FirstChildElement("Latitude"))
		if (item->FirstChildElement("Latitude")->GetText())
			device->m_latitude = item->FirstChildElement("Latitude")->GetText();

	if (item->FirstChildElement("Info"))
		if (item->FirstChildElement("Info")->FirstChildElement("PTZType"))
			if (item->FirstChildElement("Info")->FirstChildElement("PTZType")->GetText())
				device->m_ptzType =
				    atoi(item->FirstChildElement("Info")->FirstChildElement("PTZType")->GetText());

	int ss = device->m_deviceID.size();

	if (ss < 9) {
		device->m_type = CIVIL_TYPE;
	} else if (ss != 20) {
		device->m_type = UNKNOWN_TYPE;
	} else {
		int tt = stoi(device->m_deviceID.substr(10, 3));

		if (tt > 130 && tt < 133) {
			device->m_type = CAMERA_TYPE;
		} else if (tt == 601 || tt == 121) {
			device->m_type = CAMERA_TYPE;
		} else if (tt == 111) {
			device->m_type = NVR_TYPE;
		} else if (tt == 215) {
			device->m_type = BIZGROUP_TYPE;
		} else if (tt == 216) {
			device->m_type = VIRTUALGROUP_TYPE;
		} else if (tt == 200) {
			device->m_type = DOMAIN_TYPE;
		} else {
			device->m_type = UNKNOWN_TYPE;
		}
	}

	if (item->FirstChildElement("ParentID")) {
		device->m_parentID = item->FirstChildElement("ParentID")->GetText();
	} else {
		if (ss == 4 || ss == 6 || ss == 8) {
			device->m_parentID = device->m_deviceID.substr(0, ss - 2);
		} else {
			if (item->FirstChildElement("BusinessGroupID")) {
				device->m_parentID = item->FirstChildElement("BusinessGroupID")->GetText();
			} else if (device->m_deviceID != domainID) {
				device->m_parentID = domainID;
			} else {
				device->m_parentID = m_gbServerId;
			}
		}
	}

	dd->m_device[device->m_deviceID] = device;
	MsDevMgr::Instance()->AddOrUpdateDevice(device);

	MS_LOG_DEBUG("add device:%s", device->m_deviceID.c_str());
}

void MsGbServer::HandleCatalog(XMLElement *root, const char *domainID) {
	XMLElement *sn = root->FirstChildElement("SN");
	int nSn = atoi(sn->GetText());

	auto it = m_gbSessionCtx.find(nSn);
	if (it == m_gbSessionCtx.end()) {
		MS_LOG_WARN("catalog sn:%d not exist", nSn);
		return;
	}

	shared_ptr<GbSessionCtx> &ctx = it->second;
	XMLElement *sumNum = root->FirstChildElement("SumNum");
	if (sumNum == nullptr) {
		MS_LOG_ERROR("catalog sn:%d no sum num", nSn);
		return;
	}

	int nSum = atoi(sumNum->GetText());
	ctx->m_sum = nSum;
	XMLElement *devList = nullptr;
	XMLElement *item = nullptr;
	devList = root->FirstChildElement("DeviceList");
	if (devList) {
		item = devList->FirstChildElement("Item");
	}

	while (item) {
		++ctx->m_recevd;
		string devID = item->FirstChildElement("DeviceID")->GetText();

		this->AddGbDevice(item, domainID, ctx->m_domain);

		item = item->NextSiblingElement();
	}

	if (ctx->m_recevd == ctx->m_sum) {
		MS_LOG_INFO("catalog sn:%d finish total:%d", nSn, ctx->m_sum);
	} else {
		MS_LOG_INFO("catalog sn:%d recv %d/%d", nSn, ctx->m_recevd, ctx->m_sum);
		this->ResetTimer(ctx->m_timer);
	}
}

void MsGbServer::HandleRecord(XMLElement *root, const char *deviceID) {
	XMLElement *sn = root->FirstChildElement("SN");
	int nSn = atoi(sn->GetText());
	auto it = m_gbSessionCtx.find(nSn);
	if (it == m_gbSessionCtx.end()) {
		MS_LOG_WARN("record sn:%d not exist", nSn);
		return;
	}

	shared_ptr<GbSessionCtx> ctx = it->second;
	XMLElement *sumNum = root->FirstChildElement("SumNum");
	int nSum = atoi(sumNum->GetText());
	ctx->m_sum = nSum;

	XMLElement *devList = nullptr;
	XMLElement *item = nullptr;
	devList = root->FirstChildElement("RecordList");
	if (devList) {
		item = devList->FirstChildElement("Item");
	}

	while (item) {
		++ctx->m_recevd;
		json j;
		string name;

		j["deviceId"] = item->FirstChildElement("DeviceID")->GetText();

		GbkToUtf8(name, item->FirstChildElement("Name")->GetText());
		j["name"] = name;

		if (item->FirstChildElement("StartTime"))
			j["startTime"] = item->FirstChildElement("StartTime")->GetText();

		if (item->FirstChildElement("EndTime"))
			j["endTime"] = item->FirstChildElement("EndTime")->GetText();

		if (item->FirstChildElement("Type"))
			j["type"] = item->FirstChildElement("Type")->GetText();

		ctx->m_record["result"].emplace_back(j);

		item = item->NextSiblingElement();
	}

	if (ctx->m_recevd == ctx->m_sum) {
		MS_LOG_INFO("record sn:%d finish total:%d", nSn, ctx->m_sum);

		ctx->m_record["code"] = 0;
		ctx->m_record["msg"] = "success";

		if (!ctx->m_recevd) {
			ctx->m_record["result"] = json::array();
		}

		MsMsg msRsp;
		MsMsg &msg = ctx->m_req;

		msRsp.m_msgID = MS_GEN_HTTP_RSP;
		msRsp.m_sessinID = msg.m_sessinID;
		msRsp.m_dstType = msg.m_srcType;
		msRsp.m_dstID = msg.m_srcID;
		msRsp.m_strVal = ctx->m_record.dump();
		msRsp.m_intVal = msg.m_msgID;

		this->PostMsg(msRsp);

		this->DelTimer(ctx->m_timer);
		m_gbSessionCtx.erase(nSn);
	} else {
		MS_LOG_INFO("record sn:%d recv %d/%d", nSn, ctx->m_recevd, ctx->m_sum);
		this->ResetTimer(ctx->m_timer);
	}
}

void MsGbServer::HandleInvite(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr,
                              char *body, int len) {
	// not implemented yet
	MsSipMsg rspMsg;
	rspMsg.CloneBasic(sipMsg);
	rspMsg.m_status = "501";
	rspMsg.m_reason = "Not Implemented";
	if (!rspMsg.m_to.HasTag()) {
		rspMsg.m_to.AppendTag(GenRandStr(16));
	}
	SendSipMsg(rspMsg, sock, addr);
}

void MsGbServer::HandleResponse(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, char *body, int len) {
	int status = stoi(sipMsg.m_status);

	if (status >= 100 && status < 200) {
		// MS_LOG_INFO("rsp status:%d method:%s", status, sipMsg.m_cseq.GetMethond().c_str());
		return;
	}

	if (sipMsg.m_cseq.GetMethond() == "INVITE") {
		this->HandleInviteRsp(sipMsg, sock, status, body, len);
	} else {
		if (status != 200) {
			MS_LOG_INFO("rsp status:%d method:%s", status, sipMsg.m_cseq.GetMethond().c_str());
		}
	}
}

void MsGbServer::HandleInviteRsp(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, int status,
                                 char *body, int len) {

	auto it = m_inviteCtx.find(sipMsg.m_callID.m_value);
	if (it == m_inviteCtx.end()) {
		MS_LOG_WARN("invite local call:%s not exist", sipMsg.m_callID.m_value.c_str());
		return;
	}

	shared_ptr<InviteCtx> ctx = it->second;
	this->DelTimer(ctx->m_timer);
	ctx->m_timer = 0;

	if (!sipMsg.m_contact.GetPort()) {
		BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), ctx->m_dstIP, ctx->m_dstPort);
	} else {
		string rIP = sipMsg.m_contact.GetIP();
		if (ctx->m_dstIP != rIP) {
			MS_LOG_WARN("contact ip diff domain ip:%s contact ip:%s", ctx->m_dstIP.c_str(),
			            rIP.c_str());

			BuildContact(sipMsg.m_contact, sipMsg.m_contact.GetID(), ctx->m_dstIP, ctx->m_dstPort);
		}
	}

	ctx->m_rsp = sipMsg;

	MsMsg rsp;
	rsp.m_msgID = MS_INVITE_CALL_RSP;
	rsp.m_dstID = ctx->m_srcID;
	rsp.m_dstType = ctx->m_srcType;
	rsp.m_intVal = status;

	if (status == 200) {
		MS_LOG_INFO("invite local call:%s staus:%d", sipMsg.m_callID.m_value.c_str(), status);
		rsp.m_strVal = string(body, len);
		this->PostMsg(rsp);
	} else {
		MS_LOG_WARN("invite local call:%s staus:%d", sipMsg.m_callID.m_value.c_str(), status);
		string descb = "Invite failed:";
		descb += to_string(status);

		if (!strcasecmp(sipMsg.m_contentType.m_value.c_str(), "Application/MANSCDP+xml")) {
			tinyxml2::XMLDocument doc;

			if (!doc.Parse(body, len)) {
				XMLElement *root = doc.RootElement();
				XMLElement *ec = root->FirstChildElement("ErrorCode");

				if (ec) {
					descb += '-';
					descb += ec->GetText();

					MS_LOG_WARN("invite local call:%s err code:%s", sipMsg.m_callID.m_value.c_str(),
					            ec->GetText());
				}
			}
		}

		rsp.m_strVal = descb;
		this->PostMsg(rsp);
		m_inviteCtx.erase(it);
	}

	// send ack
	{
		MsSipMsg &msg = ctx->m_rsp;
		MsSipContact &con = msg.m_contact;
		string dstIP = con.GetIP();
		int dstPort = con.GetPort();
		MsSipMsg ack;

		ack.m_method = "ACK";
		ack.m_version = msg.m_version;
		ack.m_from = msg.m_from;
		ack.m_to = msg.m_to;
		ack.m_callID = msg.m_callID;
		ack.m_maxForwards.SetIntVal(70);

		BuildUri(ack.m_uri, con.GetID(), dstIP, dstPort);

		MsSipVia via("Via");
		BuildVia(via, m_ip, m_port);
		ack.m_vias.push_back(via);

		BuildCSeq(ack.m_cseq, msg.m_cseq.GetCSeq(), ack.m_method);
		BuildContact(ack.m_contact, m_gbServerId, m_ip, m_port);

		SendSipMsg(ack, sock, dstIP, dstPort);
	}
}

void MsGbServer::HandleAck(MsSipMsg &sipMsg) {
	// not implemented
	MS_LOG_ERROR("not implement ack yet");
}

void MsGbServer::HandleBye(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr) {
	do {
		auto it = m_inviteCtx.find(sipMsg.m_callID.m_value);
		if (it == m_inviteCtx.end()) {
			MS_LOG_WARN("bye call:%s already removed", sipMsg.m_callID.m_value.c_str());
			break;
		}

		shared_ptr<InviteCtx> ctx = it->second;
		this->DelTimer(ctx->m_timer);

		MsMsg rsp;
		rsp.m_msgID = MS_INVITE_CALL_RSP;
		rsp.m_dstID = ctx->m_srcID;
		rsp.m_dstType = ctx->m_srcType;
		rsp.m_intVal = 999; // bye code
		rsp.m_strVal = "client bye";
		this->PostMsg(rsp);

		m_inviteCtx.erase(it);
	} while (0);

	MsSipMsg rspMsg;
	rspMsg.CloneBasic(sipMsg);
	rspMsg.m_status = "200";
	rspMsg.m_reason = "OK";
	SendSipMsg(rspMsg, sock, addr);
}

void MsGbServer::HandleCancel(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr) {
	// not implemented
	MS_LOG_ERROR("not implement cancel yet");
	MsSipMsg rspMsg;
	rspMsg.CloneBasic(sipMsg);
	rspMsg.m_status = "501";
	rspMsg.m_reason = "Not Implemented";
	SendSipMsg(rspMsg, sock, addr);
}

void MsGbServer::HandleNotify(MsSipMsg &sipMsg, shared_ptr<MsSocket> sock, MsInetAddr &addr,
                              char *body, int len) {
	MsSipMsg rspMsg;
	rspMsg.CloneBasic(sipMsg);
	rspMsg.m_status = "200";
	rspMsg.m_reason = "OK";
	SendSipMsg(rspMsg, sock, addr);

	tinyxml2::XMLDocument doc;
	int ret = doc.Parse(body, len);
	if (ret) {
		MS_LOG_WARN("parse xml err:%d", ret);
		return;
	}

	XMLElement *root = doc.RootElement();
	XMLElement *domainID = root->FirstChildElement("DeviceID");
	XMLElement *devList = root->FirstChildElement("DeviceList");
	XMLElement *item = nullptr;

	if (devList) {
		item = devList->FirstChildElement("Item");
	}

	auto itd = m_registDomain.find(domainID->GetText());
	if (itd == m_registDomain.end()) {
		return;
	}
	shared_ptr<RegistDomain> dd = itd->second;

	while (item) {
		string devID = item->FirstChildElement("DeviceID")->GetText();

		if (!item->FirstChildElement("Event")) {
			item = item->NextSiblingElement();
			continue;
		}

		string evt = item->FirstChildElement("Event")->GetText();

		MS_LOG_DEBUG("notify device:%s evt:%s", devID.c_str(), evt.c_str());

		if (evt == "ADD") {
			this->AddGbDevice(item, domainID->GetText(), dd);
			item = item->NextSiblingElement();
			continue;
		} else {
			shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devID);
			if (!dev.get()) {
				item = item->NextSiblingElement();
				continue;
			}

			if (evt == "UPDATE") {
				this->AddGbDevice(item, domainID->GetText(), dd);
				item = item->NextSiblingElement();
				continue;
			} else if (evt == "OFF" || evt == "VLOST" || evt == "DEFECT") {
				dev->m_status = "OFF";
			} else if (evt == "DEL") {
				MS_LOG_DEBUG("event del dev:%s", dev->m_deviceID.c_str());
				if (dd->m_device.count(dev->m_deviceID)) {
					dd->m_device.erase(dev->m_deviceID);
					set<string> dx;
					dx.insert(dev->m_deviceID);
					MsDevMgr::Instance()->DeleteDevice(dx);
				}
			} else if (evt == "ON") {
				dev->m_status = "ON";
			}

			item = item->NextSiblingElement();
		}
	}
}

void MsGbServer::ByeMsg(MsSipMsg &msg, shared_ptr<MsSocket> sock) {
	MsSipMsg bye;
	MsSipContact &con = msg.m_contact;
	string dstIP = con.GetIP();
	int dstPort = con.GetPort();

	bye.m_from = msg.m_from;
	bye.m_to = msg.m_to;
	bye.m_from.m_exist = true;
	bye.m_to.m_exist = true;
	bye.m_method = "BYE";
	bye.m_version = msg.m_version;
	bye.m_callID = msg.m_callID;
	bye.m_maxForwards.SetIntVal(70);

	BuildUri(bye.m_uri, con.GetID(), dstIP, dstPort);

	MsSipVia via("Via");
	BuildVia(via, m_ip, m_port);
	bye.m_vias.push_back(via);

	BuildCSeq(bye.m_cseq, this->GenCSeq(), bye.m_method);
	BuildContact(bye.m_contact, m_gbServerId, m_ip, m_port);

	SendSipMsg(bye, sock, dstIP, dstPort);
}

void MsGbServer::HandleMediaStatus(XMLElement *root, MsSipMsg &inMsg) {
	int st = atoi(root->FirstChildElement("NotifyType")->GetText());

	if (st == 121) {
		MS_LOG_INFO("local call:%s status 121", inMsg.m_callID.m_value.c_str());

		auto it = m_inviteCtx.find(inMsg.m_callID.m_value);
		if (it != m_inviteCtx.end()) {
			shared_ptr<InviteCtx> ctx = it->second;
			this->DelTimer(ctx->m_timer);
			this->ByeMsg(ctx->m_rsp, ctx->m_domain->m_sock);

			MsMsg rsp;
			rsp.m_msgID = MS_INVITE_CALL_RSP;
			rsp.m_dstID = ctx->m_srcID;
			rsp.m_dstType = ctx->m_srcType;
			rsp.m_intVal = 121;
			rsp.m_strVal = "call closed by device";
			this->PostMsg(rsp);

			m_inviteCtx.erase(it);
		}
	}
}

void MsGbServer::HandlePreset(XMLElement *root, const char *deviceID) {
	XMLElement *sn = root->FirstChildElement("SN");
	int nSn = atoi(sn->GetText());

	auto it = m_gbSessionCtx.find(nSn);
	if (it == m_gbSessionCtx.end()) {
		MS_LOG_WARN("querypreset sn:%d not exist", nSn);
		return;
	}

	shared_ptr<GbSessionCtx> &ctx = it->second;
	XMLElement *presetList = nullptr;
	XMLElement *item = nullptr;

	presetList = root->FirstChildElement("PresetList");
	if (presetList) {
		item = presetList->FirstChildElement("Item");
	}

	json rsp;
	rsp["code"] = 0;
	rsp["msg"] = "ok";
	rsp["result"] = json::array();

	while (item) {
		json j;

		j["presetID"] = item->FirstChildElement("PresetID")->GetText();
		rsp["result"].emplace_back(j);

		item = item->NextSiblingElement();
	}

	MsMsg msRsp;
	MsMsg &msg = ctx->m_req;

	msRsp.m_msgID = MS_GEN_HTTP_RSP;
	msRsp.m_sessinID = msg.m_sessinID;
	msRsp.m_dstType = msg.m_srcType;
	msRsp.m_dstID = msg.m_srcID;
	msRsp.m_strVal = rsp.dump();

	this->PostMsg(msRsp);

	this->DelTimer(ctx->m_timer);
	m_gbSessionCtx.erase(nSn);
}

void MsGbServer::InitCatalog(const string &domainID) {
	auto it = m_registDomain.find(domainID);
	if (it == m_registDomain.end()) {
		MS_LOG_WARN("catalog:%s not exist", domainID.c_str());
		return;
	}

	shared_ptr<RegistDomain> &d = it->second;
	map<string, shared_ptr<MsGbDevice>> &devs = d->m_device;
	MsSipContact &contact = d->m_contact;
	MsSipMsg catalog;

	for (auto &dev : devs) {
		dev.second->m_refreshed = false;
	}

	BuildSipMsg(m_ip, m_port, m_gbServerId, contact.GetIP(), contact.GetPort(), contact.GetID(),
	            this->GenCSeq(), "MESSAGE", catalog);

	catalog.m_contentType.SetValue("Application/MANSCDP+xml");

	char body[512];
	int len = sprintf(body,
	                  "<?xml "
	                  "version=\"1.0\"?>\r\n<Query>\r\n<CmdType>Catalog</CmdType>\r\n<SN>%d</"
	                  "SN>\r\n<DeviceID>%s</DeviceID>\r\n</Query>\r\n",
	                  this->GenCSeq(), contact.GetID().c_str());
	catalog.SetBody(body, len);

	SendSipMsg(catalog, d->m_sock, contact.GetIP(), contact.GetPort());

	shared_ptr<GbSessionCtx> ctx = make_shared<GbSessionCtx>();
	MsMsg catMsg;

	catMsg.m_msgID = MS_CATALOG_TIME_OUT;
	catMsg.m_intVal = m_cseq;

	ctx->m_timer = this->AddTimer(catMsg, 60);
	ctx->m_recevd = ctx->m_sum = 0;
	ctx->m_domain = d;

	m_gbSessionCtx.emplace(m_cseq, ctx);

	MS_LOG_INFO("init catalog:%s sn:%d", domainID.c_str(), m_cseq);
}

void MsGbServer::HttpInitCatalog(MsMsg &msg) {
	for (auto &dd : m_registDomain) {
		this->InitCatalog(dd.first);
	}
}

void MsGbServer::InitRecordInfo(MsMsg &msg) {
	json jRsp;

	do {
		string devID, st, et;
		string recordType;

		try {
			json j = json::parse(msg.m_strVal.c_str());
			devID = j["deviceId"].get<string>();
			st = j["startTime"].get<string>();
			et = j["endTime"].get<string>();
			recordType = j["type"].is_null() ? "" : j["type"].get<string>();
		} catch (json::exception &e) {
			MS_LOG_WARN("json err:%s", e.what());
			jRsp["code"] = 1;
			jRsp["msg"] = "json error";
			break;
		}

		shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devID);
		if (!dev.get()) {
			MS_LOG_WARN("dev:%s not exist", devID.c_str());
			jRsp["code"] = 1;
			jRsp["msg"] = "dev not exist";
			break;
		}

		auto itd = m_registDomain.find(dev->m_domainID);
		if (itd == m_registDomain.end()) {
			MS_LOG_WARN("dev domain:%s not exist", dev->m_domainID.c_str());
			jRsp["code"] = 1;
			jRsp["msg"] = "dev domain not exist";
			break;
		}

		shared_ptr<RegistDomain> &domain = itd->second;
		MsSipContact &con = domain->m_contact;
		MsSipMsg record;

		BuildSipMsg(m_ip, m_port, m_gbServerId, con.GetIP(), con.GetPort(), devID, this->GenCSeq(),
		            "MESSAGE", record);

		record.m_contentType.SetValue("Application/MANSCDP+xml");

		if (recordType.size() == 0) {
			recordType = MsConfig::Instance()->GetConfigStr("queryRecordType");
		}

		char body[512];
		int len =
		    sprintf(body, "<?xml version=\"1.0\"?>\r\n<Query>\r\n<CmdType>RecordInfo</CmdType>\r\n\
<SN>%d</SN>\r\n<DeviceID>%s</DeviceID>\r\n<StartTime>%s</StartTime>\r\n<EndTime>%s</EndTime>\r\n\
<Type>%s</Type>\r\n</Query>\r\n",
		            this->GenCSeq(), devID.c_str(), st.c_str(), et.c_str(), recordType.c_str());
		record.SetBody(body, len);

		SendSipMsg(record, domain->m_sock, con.GetIP(), con.GetPort());

		shared_ptr<GbSessionCtx> ctx = make_shared<GbSessionCtx>();
		MsMsg toMsg;

		toMsg.m_sessinID = msg.m_sessinID;
		toMsg.m_msgID = MS_RECORD_TIME_OUT;
		toMsg.m_intVal = m_cseq;
		ctx->m_timer = this->AddTimer(toMsg, 25);
		ctx->m_recevd = ctx->m_sum = 0;
		ctx->m_req = msg;
		ctx->m_domain = domain;
		m_gbSessionCtx.emplace(m_cseq, ctx);

		MS_LOG_INFO("init recordinfo:%s sn:%d", devID.c_str(), m_cseq);

		return;

	} while (0);

	MsMsg msRsp;

	msRsp.m_msgID = MS_GEN_HTTP_RSP;
	msRsp.m_sessinID = msg.m_sessinID;
	msRsp.m_dstType = msg.m_srcType;
	msRsp.m_dstID = msg.m_srcID;
	msRsp.m_strVal = jRsp.dump();
	msRsp.m_intVal = msg.m_msgID;

	this->PostMsg(msRsp);
}

void MsGbServer::QueryPreset(MsMsg &msg) {
	json jRsp;

	do {
		string devID = msg.m_strVal;
		shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devID);

		if (!dev.get()) {
			MS_LOG_WARN("dev:%s not exist", devID.c_str());
			jRsp["code"] = 1;
			jRsp["msg"] = "dev not exist";
			break;
		}

		auto itd = m_registDomain.find(dev->m_domainID);
		if (itd == m_registDomain.end()) {
			MS_LOG_WARN("dev domain:%s not exist", dev->m_domainID.c_str());
			jRsp["code"] = 1;
			jRsp["msg"] = "dev domain not exist";
			break;
		}

		shared_ptr<RegistDomain> &domain = itd->second;
		MsSipContact &con = domain->m_contact;
		MsSipMsg record;

		BuildSipMsg(m_ip, m_port, m_gbServerId, con.GetIP(), con.GetPort(), devID, this->GenCSeq(),
		            "MESSAGE", record);
		record.m_contentType.SetValue("Application/MANSCDP+xml");

		char body[512];
		int len =
		    sprintf(body, "<?xml version=\"1.0\"?>\r\n<Query>\r\n<CmdType>PresetQuery</CmdType>\r\n\
<SN>%d</SN>\r\n<DeviceID>%s</DeviceID>\r\n</Query>\r\n",
		            this->GenCSeq(), devID.c_str());
		record.SetBody(body, len);

		SendSipMsg(record, domain->m_sock, con.GetIP(), con.GetPort());

		shared_ptr<GbSessionCtx> ctx = make_shared<GbSessionCtx>();
		MsMsg toMsg;

		toMsg.m_sessinID = msg.m_sessinID;
		toMsg.m_msgID = MS_QUERY_PRESET_TIMEOUT;
		toMsg.m_intVal = m_cseq;
		ctx->m_timer = this->AddTimer(toMsg, 25);
		ctx->m_req = msg;
		ctx->m_domain = domain;

		m_gbSessionCtx.emplace(m_cseq, ctx);
		MS_LOG_INFO("init querypreset:%s sn:%d", devID.c_str(), m_cseq);
		return;

	} while (0);

	MsMsg msRsp;

	msRsp.m_msgID = MS_GEN_HTTP_RSP;
	msRsp.m_sessinID = msg.m_sessinID;
	msRsp.m_dstType = msg.m_srcType;
	msRsp.m_dstID = msg.m_srcID;
	msRsp.m_strVal = jRsp.dump();

	this->PostMsg(msRsp);
}

void MsGbServer::QueryPresetTimeout(int sn) {
	MS_LOG_WARN("querypreset sn:%d time out", sn);

	auto it = m_gbSessionCtx.find(sn);
	if (it != m_gbSessionCtx.end()) {
		shared_ptr<GbSessionCtx> &x = it->second;
		MsMsg &msg = x->m_req;
		MsMsg msRsp;
		json jRsp;

		jRsp["code"] = 1;
		jRsp["msg"] = "querypreset time out";

		msRsp.m_msgID = MS_GEN_HTTP_RSP;
		msRsp.m_sessinID = msg.m_sessinID;
		msRsp.m_dstType = msg.m_srcType;
		msRsp.m_dstID = msg.m_srcID;
		msRsp.m_strVal = jRsp.dump();

		this->PostMsg(msRsp);

		m_gbSessionCtx.erase(it);
	}
}

void MsGbServer::PtzControl(MsMsg &msg) {
	SPtzCmd *pp = static_cast<SPtzCmd *>(msg.m_ptr);

	string devID = pp->m_devid;
	int nCmd = pp->m_ptzCmd;
	int timeout = pp->m_timeout;
	delete pp;

	int p1 = 165;
	int p2 = 15;
	int p3 = 77;

	int p4 = 0;
	int p5 = 0;
	int p6 = 0;
	int p7 = 0;

	char buf[64];
	string ptzCmd;

	switch (nCmd) {
	case 1: // left
	case 2: // right
	{
		p4 = nCmd == 1 ? 2 : 1;
		p5 = 128;
		p6 = 0;
		p7 = 0;
	} break;

	case 3: // up
	case 4: // down
	{
		p4 = nCmd == 3 ? 8 : 4;
		p5 = 0;
		p6 = 128;
		p7 = 0;
	} break;

	case 5: // zoom in
	case 6: // zoom out
	{
		p4 = nCmd == 5 ? 16 : 32;
		p5 = 0;
		p6 = 0;
		p7 = 128;
	} break;

	case 11: // left up
	case 12: // right up
	case 13: // left down
	case 14: // right down
	{
		if (nCmd == 11)
			p4 = 10;
		else if (nCmd == 12)
			p4 = 9;
		else if (nCmd == 13)
			p4 = 6;
		else if (nCmd == 14)
			p4 = 5;

		p5 = 128;
		p6 = 128;
		p7 = 0;
	} break;

	case 7: // goto preset
	case 8: // set preset
	case 9: // del preset
	{
		if (nCmd == 7) {
			p4 = 0x82;
		} else if (nCmd == 8) {
			p4 = 0x81;
		} else {
			p4 = 0x83;
		}

		int presetid = atoi(pp->m_presetID.c_str());
		if (presetid <= 0 || presetid > 255) {
			presetid = 1;
		}

		p5 = 0;
		p6 = presetid;
		p7 = 0;
		timeout = 0;
	} break;

	default:
		return;
		break;
	}

	int p8 = (p1 + p2 + p3 + p4 + p5 + p6 + p7) % 256;
	sprintf(buf, "%02X%02X%02X%02X%02X%02X%02X%02X", p1, p2, p3, p4, p5, p6, p7, p8);
	ptzCmd = buf;

	shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devID);
	if (!dev.get()) {
		MS_LOG_WARN("dev:%s not exist", devID.c_str());
		return;
	}

	auto itd = m_registDomain.find(dev->m_domainID);
	if (itd == m_registDomain.end()) {
		MS_LOG_WARN("dev domain:%s not exist", dev->m_domainID.c_str());
		return;
	}

	shared_ptr<RegistDomain> domain = itd->second;

	thread dd(&MsGbServer::DoPtz, dynamic_pointer_cast<MsGbServer>(shared_from_this()), domain,
	          devID, ptzCmd, timeout);
	dd.detach();
}

void MsGbServer::DoPtz(shared_ptr<RegistDomain> domain, string devID, string ptzCmd, int timeout) {
	MsSipContact &con = domain->m_contact;
	MsSipMsg record;

	BuildSipMsg(m_ip, m_port, m_gbServerId, con.GetIP(), con.GetPort(), devID, this->GenCSeq(),
	            "MESSAGE", record);

	record.m_contentType.SetValue("Application/MANSCDP+xml");

	char body[512];
	int len =
	    sprintf(body, "<?xml version=\"1.0\"?>\r\n<Control>\r\n<CmdType>DeviceControl</CmdType>\r\n\
<SN>%d</SN>\r\n<DeviceID>%s</DeviceID>\r\n<PTZCmd>%s</PTZCmd>\r\n<Info><ControlPriority>5</ControlPriority></Info>\r\n\
</Control>\r\n",
	            this->GenCSeq(), devID.c_str(), ptzCmd.c_str());
	record.SetBody(body, len);

	SendSipMsg(record, domain->m_sock, con.GetIP(), con.GetPort());

	if (timeout) {
		SleepMs(timeout);

		ptzCmd = "A50F4D0000000001";
		timeout = 0;

		this->DoPtz(domain, devID, ptzCmd, timeout);
	}
}

void MsGbServer::ClearDomain(shared_ptr<RegistDomain> domain) {
	this->DelTimer(domain->m_timer);
	domain->m_timer = 0;

	for (auto &dd : domain->m_device) {
		dd.second->m_status = "OFF";
	}

	for (auto it = m_inviteCtx.begin(); it != m_inviteCtx.end();) {
		shared_ptr<InviteCtx> ctx = it->second;
		if (ctx->m_domain == domain) {
			this->ByeMsg(ctx->m_rsp, domain->m_sock);
			this->DelTimer(ctx->m_timer);

			MsMsg rsp;
			rsp.m_dstID = ctx->m_srcID;
			rsp.m_dstType = ctx->m_srcType;
			rsp.m_msgID = MS_INVITE_CALL_RSP;
			rsp.m_intVal = 998;
			rsp.m_strVal = "domain unregistered";
			this->PostMsg(rsp);

			it = m_inviteCtx.erase(it);
		} else {
			++it;
		}
	}

	for (auto it = m_gbSessionCtx.begin(); it != m_gbSessionCtx.end();) {
		shared_ptr<GbSessionCtx> &ctx = it->second;
		if (ctx->m_domain == domain) {
			this->DelTimer(ctx->m_timer);
			it = m_gbSessionCtx.erase(it);
		} else {
			++it;
		}
	}
}

int MsGbServer::GenCSeq() {
	if (++m_cseq <= 0) {
		m_cseq = 1;
	}

	return m_cseq;
}

void MsGbServer::OnRegTimeout(string &id) {
	MS_LOG_INFO("reg:%s time out", id.c_str());

	auto it = m_registDomain.find(id);
	if (it != m_registDomain.end()) {
		this->ClearDomain(it->second);
		m_registDomain.erase(it);
	}
}

void MsGbServer::OnCataTimeout(int sn) {
	MS_LOG_WARN("catalog sn:%d time out", sn);

	auto it = m_gbSessionCtx.find(sn);

	if (it != m_gbSessionCtx.end()) {
		shared_ptr<GbSessionCtx> &ctx = it->second;

		if (ctx->m_recevd < ctx->m_sum || ctx->m_sum == 0) {
			return;
		}

		set<string> delDev;
		map<string, shared_ptr<MsGbDevice>> &devs = ctx->m_domain->m_device;

		for (auto &dev : devs) {
			if (!dev.second->m_refreshed) {
				MS_LOG_DEBUG("catalog refersh del dev:%s", dev.second->m_deviceID.c_str());
				delDev.insert(dev.second->m_deviceID);
			}
		}

		for (auto &dev : delDev) {
			devs.erase(dev);
		}

		MsDevMgr::Instance()->DeleteDevice(delDev);

		m_gbSessionCtx.erase(it);
	}
}

void MsGbServer::OnRecordTimeout(int sn) {
	MS_LOG_WARN("record sn:%d time out", sn);

	auto it = m_gbSessionCtx.find(sn);
	if (it != m_gbSessionCtx.end()) {
		shared_ptr<GbSessionCtx> &x = it->second;
		MsMsg &msg = x->m_req;
		MsMsg msRsp;

		json jRsp;

		jRsp["code"] = 1;
		jRsp["msg"] = "init record time out";

		msRsp.m_msgID = MS_GEN_HTTP_RSP;
		msRsp.m_sessinID = msg.m_sessinID;
		msRsp.m_dstType = msg.m_srcType;
		msRsp.m_dstID = msg.m_srcID;
		msRsp.m_strVal = jRsp.dump();
		msRsp.m_intVal = msg.m_msgID;

		this->PostMsg(msRsp);

		m_gbSessionCtx.erase(it);
	}
}

void MsGbServer::OnInviteTimeout(string &val) {
	MS_LOG_WARN("invite local call:%s time out", val.c_str());

	auto it = m_inviteCtx.find(val);
	if (it == m_inviteCtx.end()) {
		MS_LOG_WARN("invite local call:%s not exist", val.c_str());
		return;
	}

	shared_ptr<InviteCtx> ctx = it->second;

	MsMsg rsp;
	rsp.m_dstID = ctx->m_srcID;
	rsp.m_dstType = ctx->m_srcType;
	rsp.m_msgID = MS_INVITE_CALL_RSP;
	rsp.m_intVal = 1;
	rsp.m_strVal = "invite time out";
	this->PostMsg(rsp);

	m_inviteCtx.erase(it);
}

void MsGbServer::GetRegistDomain(MsMsg &msg) {
	json j;
	j["code"] = 0;
	j["msg"] = "success";
	j["result"] = json::array();

	for (auto it = m_registDomain.begin(); it != m_registDomain.end(); ++it) {
		shared_ptr<RegistDomain> &domain = it->second;
		json dd;

		dd["id"] = domain->m_contact.GetID();
		dd["devNum"] = domain->m_device.size();
		dd["ip"] = domain->m_contact.GetIP();
		dd["port"] = domain->m_contact.GetPort();
		j["result"].emplace_back(dd);
	}

	MsMsg rsp;
	rsp.m_msgID = MS_GEN_HTTP_RSP;
	rsp.m_sessinID = msg.m_sessinID;
	rsp.m_dstType = msg.m_srcType;
	rsp.m_dstID = msg.m_srcID;
	rsp.m_strVal = j.dump();
	this->PostMsg(rsp);
}

void MsGbServer::InitInvite(MsMsg &msg) {
	SGbContext *p = static_cast<SGbContext *>(msg.m_ptr);

	string &reqID = p->gbID;
	int &transport = p->transport;
	string rtpIP = p->rtpIP;
	int &rtpPort = p->rtpPort;
	int &type = p->type;
	string &st = p->startTime;
	string &et = p->endTime;

	auto dev = MsDevMgr::Instance()->FindDevice(reqID);
	if (!dev) {
		MS_LOG_WARN("invite dev:%s not exist", reqID.c_str());
		MsMsg iRsp;
		iRsp.m_msgID = MS_INVITE_CALL_RSP;
		iRsp.m_dstType = msg.m_srcType;
		iRsp.m_dstID = msg.m_srcID;
		iRsp.m_intVal = 1;
		iRsp.m_strVal = "device not exist";
		this->PostMsg(iRsp);
		return;
	}

	auto itd = m_registDomain.find(dev->m_domainID);
	if (itd == m_registDomain.end()) {
		MS_LOG_WARN("invite dev domain:%s not exist", dev->m_domainID.c_str());
		MsMsg iRsp;
		iRsp.m_msgID = MS_INVITE_CALL_RSP;
		iRsp.m_dstType = msg.m_srcType;
		iRsp.m_dstID = msg.m_srcID;
		iRsp.m_intVal = 1;
		iRsp.m_strVal = "device domain not exist";
		this->PostMsg(iRsp);
		return;
	}

	shared_ptr<RegistDomain> &domain = itd->second;
	MsSipContact &con = domain->m_contact;
	string dstIP = con.GetIP();
	int dstPort = con.GetPort();

	MsSipMsg invite;
	BuildSipMsg(m_ip, m_port, m_gbServerId, dstIP, dstPort, reqID, this->GenCSeq(), "INVITE",
	            invite);
	BuildContact(invite.m_contact, m_gbServerId, m_ip, m_port);
	BuildSubject(invite.m_subject, reqID, m_gbServerId, type == 0);
	invite.m_contentType.SetValue("application/sdp");

	string mapIP = MsDevMgr::Instance()->GetMapIP(rtpIP);
	if (mapIP.size()) {
		MS_LOG_INFO("map ip %s->%s", rtpIP.c_str(), mapIP.c_str());
		rtpIP = mapIP;
	}

	string sdp = "v=0\r\no=";
	sdp += reqID;
	sdp += " 0 0 IN IP4 ";
	sdp += rtpIP;
	sdp += "\r\ns=";

	if (!type) {
		sdp += "Play";
	} else {
		sdp += "Playback\r\nu=";
		sdp += reqID;
		sdp += ':';
		sdp += to_string(type);
	}

	sdp += "\r\nc=IN IP4 ";
	sdp += rtpIP;
	sdp += "\r\nt=";
	sdp += st;
	sdp += ' ';
	sdp += et;
	sdp += "\r\nm=video ";
	sdp += to_string(rtpPort);

	if (transport == EN_UDP) {
		sdp += " RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\na=recvonly\r\n";
	} else {
		sdp += " TCP/RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\n";

		if (transport == EN_TCP_ACTIVE) {
			sdp += "a=setup:active\r\n";
		} else // transport == EN_TCP_PASSIVE
		{
			sdp += "a=setup:passive\r\n";
		}

		sdp += "a=connection:new\r\na=recvonly\r\n";
	}

	invite.SetBody(sdp.c_str(), sdp.size());

	SendSipMsg(invite, domain->m_sock, dstIP, dstPort);

	shared_ptr<InviteCtx> x = make_shared<InviteCtx>();
	MsMsg ito;

	x->m_srcType = msg.m_srcType;
	x->m_srcID = msg.m_srcID;
	x->m_req = invite;
	x->m_rsp = invite;
	x->m_domain = domain;
	x->m_dstIP = dstIP;
	x->m_dstPort = dstPort;
	x->m_startTime = GmtTimeToStr(stoll(st) + 8 * 3600);
	x->m_endTime = GmtTimeToStr(stoll(et) + 8 * 3600);
	ito.m_msgID = MS_INVITE_TIME_OUT;
	ito.m_strVal = invite.m_callID.m_value;
	x->m_timer = this->AddTimer(ito, 25);

	m_inviteCtx.emplace(invite.m_callID.m_value, x);

	MsMsg iRsp;
	iRsp.m_msgID = MS_INVITE_CALL_RSP;
	iRsp.m_dstType = msg.m_srcType;
	iRsp.m_dstID = msg.m_srcID;
	iRsp.m_intVal = 100;
	iRsp.m_strVal = invite.m_callID.m_value;
	this->PostMsg(iRsp);

	MS_LOG_INFO("gb server invite:%s call:%s", reqID.c_str(), invite.m_callID.m_value.c_str());
}