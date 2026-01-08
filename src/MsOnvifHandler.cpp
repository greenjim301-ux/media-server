#include "MsOnvifHandler.h"
#include "MsDevMgr.h"
#include "MsHttpMsg.h"
#include "MsLog.h"
#include "MsSha1.h"
#include "MsSocket.h"

#include <string.h>
#include <thread>

MsOnvifHandler::MsOnvifHandler(shared_ptr<MsReactor> r, shared_ptr<MsGbDevice> dev, int sid)
    : m_reactor(r), m_nrecv(0), m_stage(STAGE_S1), m_dev(dev), m_sid(sid) {
	m_buf = (char *)malloc(DEF_BUF_SIZE);
}

MsOnvifHandler::~MsOnvifHandler() {
	MS_LOG_DEBUG("~MsOnvifHandler");
	free(m_buf);
}

void MsOnvifHandler::HandleRead(shared_ptr<MsEvent> evt) {
	MS_LOG_INFO("handle read");

	MsSocket *sock = evt->GetSocket();
	int ret = sock->Recv(m_buf + m_nrecv, DEF_BUF_SIZE - m_nrecv);
	if (ret < 1) {
		MS_LOG_INFO("read close:%d", sock->GetFd());
		this->clear_evt(evt);
		return;
	}

	m_nrecv += ret;
	m_buf[m_nrecv] = '\0';

	MS_LOG_INFO("%s", m_buf);

	char *p = strstr(m_buf, "Envelope>");
	if (!p) // not full msg recved
	{
		return;
	}

	switch (m_stage) {
	case STAGE_S1:
		this->proc_s1(evt);
		break;

	case STAGE_S2:
		this->proc_s2(evt);
		break;

	case STAGE_S3:
		this->proc_s3(evt);
		break;

	case STAGE_S4:
		this->proc_s4(evt);
		break;

	default:
		break;
	}
}

void MsOnvifHandler::HandleClose(shared_ptr<MsEvent> evt) { this->clear_evt(evt); }

void MsOnvifHandler::OnvifPtzControl(string user, string passwd, string url, string profile,
                                     string presetID, int cmd, int tout) {
	if (cmd == -1 && tout > 0) {
		SleepMs(tout);
	}

	string ip, uri;
	int port;

	if (parse_uri(url, ip, port, uri)) {
		MS_LOG_ERROR("url err:%s", url.c_str());
		return;
	}

	shared_ptr<MsSocket> tcp_sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);
	string host = ip + ":" + to_string(port);

	int ret = tcp_sock->Connect(ip, port);
	if (ret < 0) {
		MS_LOG_ERROR("connect err:%s", host.c_str());
		return;
	}

	string nonce, created, digest;
	gen_digest(passwd, created, nonce, digest);

	char *buf = new char[DEF_BUF_SIZE];

	if ((cmd >= 1 && cmd <= 6) || (cmd >= 11 && cmd <= 14)) {
		string x = "0";
		string y = "0";
		string z = "0";

		switch (cmd) {
		case 1:
			x = "-0.5";
			break;

		case 2:
			x = "0.5";
			break;

		case 3:
			y = "0.5";
			break;

		case 4:
			y = "-0.5";
			break;

		case 5:
			z = "0.5";
			break;

		case 6:
			z = "-0.5";
			break;

		case 11:
			x = "-0.5";
			y = "0.5";
			break;

		case 12:
			x = "0.5";
			y = "0.5";
			break;

		case 13:
			x = "-0.5";
			y = "-0.5";
			break;

		case 14:
			x = "0.5";
			y = "-0.5";
			break;

		default:
			break;
		}

		ret =
		    sprintf(buf,
		            "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
		            "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
		            "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
		            "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:"
		            "Username>%s</wsse:Username><wsse:Password "
		            "Type=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
		            "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
		            "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
		            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		            "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><ContinuousMove "
		            "xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>%s</"
		            "ProfileToken><Velocity><PanTilt x=\"%s\" y=\"%s\" "
		            "space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace\" "
		            "xmlns=\"http://www.onvif.org/ver10/schema\"/><Zoom x=\"%s\" "
		            "space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace\" "
		            "xmlns=\"http://www.onvif.org/ver10/schema\"/></Velocity></ContinuousMove></"
		            "s:Body></s:Envelope>",
		            user.c_str(), digest.c_str(), nonce.c_str(), created.c_str(), profile.c_str(),
		            x.c_str(), y.c_str(), z.c_str());
	} else if (cmd == 7) {
		ret =
		    sprintf(buf,
		            "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
		            "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
		            "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
		            "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:"
		            "Username>%s</wsse:Username><wsse:Password "
		            "Type=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
		            "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
		            "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
		            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		            "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><GotoPreset "
		            "xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>%s</"
		            "ProfileToken><PresetToken>%s</PresetToken></GotoPreset></s:Body></s:Envelope>",
		            user.c_str(), digest.c_str(), nonce.c_str(), created.c_str(), profile.c_str(),
		            presetID.c_str());
	} else if (cmd == 8) {
		ret =
		    sprintf(buf,
		            "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
		            "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
		            "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
		            "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:"
		            "Username>%s</wsse:Username><wsse:Password "
		            "Type=\"http://docs.oasis-open.org/wss/2004/01/"
		            "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
		            "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
		            "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
		            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		            "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><SetPreset "
		            "xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>%s</"
		            "ProfileToken><PresetToken>%s</PresetToken></SetPreset></s:Body></s:Envelope>",
		            user.c_str(), digest.c_str(), nonce.c_str(), created.c_str(), profile.c_str(),
		            presetID.c_str());
	} else if (cmd == 9) {
		ret = sprintf(
		    buf,
		    "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
		    "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
		    "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
		    "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
		    "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
		    "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:Username>%s</"
		    "wsse:Username><wsse:Password "
		    "Type=\"http://docs.oasis-open.org/wss/2004/01/"
		    "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
		    "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
		    "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
		    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><RemovePreset "
		    "xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>%s</"
		    "ProfileToken><PresetToken>%s</PresetToken></RemovePreset></s:Body></s:Envelope>",
		    user.c_str(), digest.c_str(), nonce.c_str(), created.c_str(), profile.c_str(),
		    presetID.c_str());
	} else if (cmd == -1) {
		ret = sprintf(
		    buf,
		    "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
		    "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
		    "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
		    "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
		    "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
		    "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:Username>%s</"
		    "wsse:Username><wsse:Password "
		    "Type=\"http://docs.oasis-open.org/wss/2004/01/"
		    "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
		    "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
		    "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
		    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><Stop "
		    "xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>%s</"
		    "ProfileToken><PanTilt>true</PanTilt><Zoom>true</Zoom></Stop></s:Body></s:Envelope>",
		    user.c_str(), digest.c_str(), nonce.c_str(), created.c_str(), profile.c_str());
	}

	MsHttpMsg req;
	string strReq;

	req.m_method = "POST";
	req.m_uri = uri;
	req.m_version = "HTTP/1.1";
	req.m_host.SetValue(host);
	req.m_connection.SetValue("close");
	req.m_contentType.SetValue("application/soap+xml; charset=utf-8");
	req.SetBody(buf, ret);
	req.Dump(strReq);

	ret = tcp_sock->Send(strReq.c_str(), strReq.size());
	if (ret < 0) {
		MS_LOG_ERROR("send err:%s", strReq.c_str());
		delete[] buf;
		return;
	}

	if ((cmd >= 1 && cmd <= 6) || (cmd >= 11 && cmd <= 14)) {
		thread work(MsOnvifHandler::OnvifPtzControl, user, passwd, url, profile, presetID, -1,
		            tout);
		work.detach();
	}

	while (true) {
		ret = tcp_sock->Recv(buf, DEF_BUF_SIZE);
		if (ret < 1) {
			delete[] buf;
			break;
		}

		buf[ret] = '\0';
		char *p = strstr(buf, "Envelope>");
		if (p) {
			delete[] buf;
			break;
		}
	}

	MS_LOG_DEBUG("ptz:%d finish", cmd);
}

void MsOnvifHandler::QueryPreset(string user, string passwd, string url, string profile, int nseq) {
	json rsp;
	rsp["code"] = 0;
	rsp["msg"] = "ok";
	rsp["result"] = json::array();

	MsMsg msRsp;
	msRsp.m_msgID = MS_GEN_HTTP_RSP;
	msRsp.m_sessinID = nseq;
	msRsp.m_dstType = MS_HTTP_SERVER;
	msRsp.m_dstID = 1;

	string ip, uri;
	int port;

	if (parse_uri(url, ip, port, uri)) {
		MS_LOG_ERROR("url err:%s", url.c_str());
		rsp["code"] = 1;
		rsp["msg"] = "url err";
		msRsp.m_strVal = rsp.dump();
		MsReactorMgr::Instance()->PostMsg(msRsp);

		return;
	}

	shared_ptr<MsSocket> tcp_sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);
	string host = ip + ":" + to_string(port);

	int ret = tcp_sock->Connect(ip, port);
	if (ret < 0) {
		MS_LOG_ERROR("connect err:%s", host.c_str());
		rsp["code"] = 1;
		rsp["msg"] = "connect err";
		msRsp.m_strVal = rsp.dump();
		MsReactorMgr::Instance()->PostMsg(msRsp);

		return;
	}

	string nonce, created, digest;
	gen_digest(passwd, created, nonce, digest);

	char *buf = new char[DEF_BUF_SIZE];

	ret = sprintf(buf,
	              "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
	              "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
	              "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
	              "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:"
	              "Username>%s</wsse:Username><wsse:Password "
	              "Type=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
	              "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
	              "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
	              "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
	              "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><GetPresets "
	              "xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>%s</ProfileToken></"
	              "GetPresets></s:Body></s:Envelope>",
	              user.c_str(), digest.c_str(), nonce.c_str(), created.c_str(), profile.c_str());

	MsHttpMsg req;
	string strReq;

	req.m_method = "POST";
	req.m_uri = uri;
	req.m_version = "HTTP/1.1";
	req.m_host.SetValue(host);
	req.m_connection.SetValue("close");
	req.m_contentType.SetValue("application/soap+xml; charset=utf-8");
	req.SetBody(buf, ret);
	req.Dump(strReq);

	ret = tcp_sock->Send(strReq.c_str(), strReq.size());
	if (ret < 0) {
		MS_LOG_ERROR("send err:%s", strReq.c_str());
		rsp["code"] = 1;
		rsp["msg"] = "send err";
		msRsp.m_strVal = rsp.dump();
		MsReactorMgr::Instance()->PostMsg(msRsp);

		delete[] buf;
		return;
	}

	int nrecv = 0;
	while (true) {
		ret = tcp_sock->Recv(buf + nrecv, DEF_BUF_SIZE - nrecv);
		if (ret < 1) {
			delete[] buf;
			break;
		}

		nrecv += ret;
		buf[nrecv] = '\0';
		char *p = strstr(buf, "Envelope>");
		if (p) {
			MS_LOG_DEBUG("presets:%s", buf);
			char *pbuf = buf;

			while (true) {
				p = strstr(pbuf, "Preset token=\"");
				if (!p) {
					break;
				}

				p += strlen("Preset token=\"");
				char *p1 = p;

				while (*p != '"')
					++p;

				json j;
				string presetToken(p1, p - p1);

				j["presetID"] = presetToken;
				rsp["result"].emplace_back(j);

				pbuf = p + 1;
			}

			delete[] buf;
			break;
		}
	}

	msRsp.m_strVal = rsp.dump();
	MsReactorMgr::Instance()->PostMsg(msRsp);
}

void MsOnvifHandler::proc_s1(shared_ptr<MsEvent> evt) {
	char *p = strstr(m_buf, "XAddrs>");
	if (!p) {
		MS_LOG_ERROR("buf err:%s", m_buf);
		this->clear_evt(evt);
		return;
	}

	p += strlen("XAddrs>");
	char *p1 = p;
	while (*p != ' ' && *p != '<')
		++p;

	string devurl(p1, p - p1);
	MS_LOG_DEBUG("device url:%s", devurl.c_str());

	string ip, uri;
	int port;

	if (parse_uri(devurl, ip, port, uri)) {
		MS_LOG_ERROR("devurl err:%s", devurl.c_str());
		this->clear_evt(evt);
		return;
	}

	if (m_dev->m_port == 0) {
		m_dev->m_port = port;
	}

	string nonce, created, digest;
	gen_digest(m_dev->m_pass, created, nonce, digest);

	shared_ptr<MsSocket> tcp_sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);
	string host = ip + ":" + to_string(port);

	int ret = tcp_sock->Connect(ip, port);
	if (ret < 0) {
		MS_LOG_ERROR("connect err:%s", host.c_str());
		this->clear_evt(evt);
		return;
	}

	ret = sprintf(m_buf,
	              "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
	              "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
	              "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
	              "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:"
	              "Username>%s</wsse:Username><wsse:Password "
	              "Type=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
	              "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
	              "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
	              "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
	              "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><GetServices "
	              "xmlns=\"http://www.onvif.org/ver10/device/wsdl\"><IncludeCapability>false</"
	              "IncludeCapability></GetServices></s:Body></s:Envelope>",
	              m_dev->m_user.c_str(), digest.c_str(), nonce.c_str(), created.c_str());

	MsHttpMsg req;
	string strReq;

	req.m_method = "POST";
	req.m_uri = uri;
	req.m_version = "HTTP/1.1";
	req.m_host.SetValue(host);
	req.m_connection.SetValue("close");
	req.m_contentType.SetValue("application/soap+xml; charset=utf-8");
	req.SetBody(m_buf, ret);
	req.Dump(strReq);

	ret = tcp_sock->Send(strReq.c_str(), strReq.size());
	if (ret < 0) {
		MS_LOG_ERROR("send err:%s", strReq.c_str());
		this->clear_evt(evt);
		return;
	}

	printf("%s\n", strReq.c_str());

	shared_ptr<MsEvent> nevt =
	    make_shared<MsEvent>(tcp_sock, MS_FD_READ | MS_FD_CLOSE, shared_from_this());

	m_reactor->AddEvent(nevt);
	m_reactor->DelEvent(m_evt);
	m_evt = nevt;
	m_stage = STAGE_S2;
	m_nrecv = 0;
}

void MsOnvifHandler::proc_s2(shared_ptr<MsEvent> evt) {
	char *p = strstr(m_buf, "Namespace>http://www.onvif.org/ver10/media/wsdl");
	if (!p) {
		MS_LOG_ERROR("buf err:%s", m_buf);
		this->clear_evt(evt);
		return;
	}

	p += strlen("Namespace>http://www.onvif.org/ver10/media/wsdl");
	p = strstr(p, "XAddr>");
	if (!p) {
		MS_LOG_ERROR("buf err:%s", m_buf);
		this->clear_evt(evt);
		return;
	}

	p += strlen("XAddr>");
	char *p1 = p;
	while (*p != ' ' && *p != '<')
		++p;

	m_mediaurl.assign(p1, p - p1);
	MS_LOG_DEBUG("media url:%s", m_mediaurl.c_str());

	p = strstr(m_buf, "Namespace>http://www.onvif.org/ver20/ptz/wsdl");
	if (!p) {
		MS_LOG_ERROR("buf no ptz:%s", m_buf);
	} else {
		p += strlen("Namespace>http://www.onvif.org/ver20/ptz/wsdl");
		p = strstr(p, "XAddr>");
		if (!p) {
			MS_LOG_ERROR("buf err:%s", m_buf);
			this->clear_evt(evt);
			return;
		}

		p += strlen("XAddr>");
		p1 = p;
		while (*p != ' ' && *p != '<')
			++p;

		m_ptzurl.assign(p1, p - p1);
		MS_LOG_DEBUG("ptz url:%s", m_ptzurl.c_str());
	}

	string ip, uri;
	int port;
	if (parse_uri(m_mediaurl, ip, port, uri)) {
		MS_LOG_ERROR("media url err:%s", m_mediaurl.c_str());
		this->clear_evt(evt);
		return;
	}

	string nonce, created, digest;
	gen_digest(m_dev->m_pass, created, nonce, digest);

	shared_ptr<MsSocket> tcp_sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);
	string host = ip + ":" + to_string(port);

	int ret = tcp_sock->Connect(ip, port);
	if (ret < 0) {
		MS_LOG_ERROR("connect err:%s", host.c_str());
		this->clear_evt(evt);
		return;
	}

	ret = sprintf(m_buf,
	              "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
	              "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
	              "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
	              "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:"
	              "Username>%s</wsse:Username><wsse:Password "
	              "Type=\"http://docs.oasis-open.org/wss/2004/01/"
	              "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
	              "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
	              "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
	              "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
	              "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><GetProfiles "
	              "xmlns=\"http://www.onvif.org/ver10/media/wsdl\"/></s:Body></s:Envelope>",
	              m_dev->m_user.c_str(), digest.c_str(), nonce.c_str(), created.c_str());

	MsHttpMsg req;
	string strReq;

	req.m_method = "POST";
	req.m_uri = uri;
	req.m_version = "HTTP/1.1";
	req.m_host.SetValue(host);
	req.m_connection.SetValue("close");
	req.m_contentType.SetValue("application/soap+xml; charset=utf-8");
	req.SetBody(m_buf, ret);
	req.Dump(strReq);

	ret = tcp_sock->Send(strReq.c_str(), strReq.size());
	if (ret < 0) {
		MS_LOG_ERROR("send err:%s", strReq.c_str());
		this->clear_evt(evt);
		return;
	}

	shared_ptr<MsEvent> nevt =
	    make_shared<MsEvent>(tcp_sock, MS_FD_READ | MS_FD_CLOSE, shared_from_this());

	m_reactor->AddEvent(nevt);
	m_reactor->DelEvent(m_evt);
	m_evt = nevt;
	m_stage = STAGE_S3;
	m_nrecv = 0;
}

void MsOnvifHandler::proc_s3(shared_ptr<MsEvent> evt) {
	char *p = strstr(m_buf, "Profiles token=\"");
	if (!p) {
		MS_LOG_ERROR("buf err:%s", m_buf);
		this->clear_evt(evt);
		return;
	}

	p += strlen("Profiles token=\"");
	char *p1 = p;

	while (*p != '"')
		++p;

	m_profile.assign(p1, p - p1);

	MS_LOG_DEBUG("profile:%s", m_profile.c_str());

	string ip, uri;
	int port;
	if (parse_uri(m_mediaurl, ip, port, uri)) {
		MS_LOG_ERROR("media url err:%s", m_mediaurl.c_str());
		this->clear_evt(evt);
		return;
	}

	string nonce, created, digest;
	gen_digest(m_dev->m_pass, created, nonce, digest);

	shared_ptr<MsSocket> tcp_sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);
	string host = ip + ":" + to_string(port);

	int ret = tcp_sock->Connect(ip, port);
	if (ret < 0) {
		MS_LOG_ERROR("connect err:%s", host.c_str());
		this->clear_evt(evt);
		return;
	}

	ret = sprintf(
	    m_buf,
	    "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope "
	    "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header><wsse:Security "
	    "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
	    "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
	    "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
	    "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:Username>%s</"
	    "wsse:Username><wsse:Password "
	    "Type=\"http://docs.oasis-open.org/wss/2004/01/"
	    "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</"
	    "wsse:Password><wsse:Nonce>%s</wsse:Nonce><wsu:Created>%s</wsu:Created></"
	    "wsse:UsernameToken></wsse:Security></s:Header><s:Body "
	    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
	    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><GetStreamUri "
	    "xmlns=\"http://www.onvif.org/ver10/media/wsdl\"><StreamSetup><Stream "
	    "xmlns=\"http://www.onvif.org/ver10/schema\">RTP-Unicast</Stream><Transport "
	    "xmlns=\"http://www.onvif.org/ver10/schema\"><Protocol>UDP</Protocol></Transport></"
	    "StreamSetup><ProfileToken>%s</ProfileToken></GetStreamUri></s:Body></s:Envelope>",
	    m_dev->m_user.c_str(), digest.c_str(), nonce.c_str(), created.c_str(), m_profile.c_str());

	MsHttpMsg req;
	string strReq;

	req.m_method = "POST";
	req.m_uri = uri;
	req.m_version = "HTTP/1.1";
	req.m_host.SetValue(host);
	req.m_connection.SetValue("close");
	req.m_contentType.SetValue("application/soap+xml; charset=utf-8");
	req.SetBody(m_buf, ret);
	req.Dump(strReq);

	ret = tcp_sock->Send(strReq.c_str(), strReq.size());
	if (ret < 0) {
		MS_LOG_ERROR("send err:%s", strReq.c_str());
		this->clear_evt(evt);
		return;
	}

	shared_ptr<MsEvent> nevt =
	    make_shared<MsEvent>(tcp_sock, MS_FD_READ | MS_FD_CLOSE, shared_from_this());

	m_reactor->AddEvent(nevt);
	m_reactor->DelEvent(m_evt);
	m_evt = nevt;
	m_stage = STAGE_S4;
	m_nrecv = 0;
}

void MsOnvifHandler::proc_s4(shared_ptr<MsEvent> evt) {
	char *p = strstr(m_buf, ":Uri>");
	if (!p) {
		MS_LOG_ERROR("buf err:%s", m_buf);
		this->clear_evt(evt);
		return;
	}

	p += strlen(":Uri>");
	char *p1 = p;

	while (*p != '<')
		++p;

	m_rtsp.assign(p1, p - p1);

	MS_LOG_DEBUG("rtsp url:%s", m_rtsp.c_str());

	m_reactor->DelEvent(m_evt);
	m_evt.reset();

	string rtspurl =
	    "rtsp://" + m_dev->m_user + ":" + m_dev->m_pass + "@" + m_rtsp.substr(strlen("rtsp://"));

	ModDev mod;
	mod.m_url = rtspurl;
	mod.m_onvifptzurl = m_ptzurl;
	mod.m_onvifprofile = m_profile;

	MsDevMgr::Instance()->ModifyDevice(m_dev->m_deviceID, mod);

	MsMsg xm;
	xm.m_msgID = MS_ONVIF_PROBE_FINISH;
	xm.m_strVal = m_dev->m_deviceID;
	m_reactor->EnqueMsg(xm);
}

void MsOnvifHandler::clear_evt(shared_ptr<MsEvent> evt) {
	if (m_evt.get()) {
		if (evt->GetSocket()->GetFd() == m_evt->GetSocket()->GetFd()) {
			m_evt.reset();
		}
	}

	m_reactor->DelEvent(evt);
}

int MsOnvifHandler::parse_uri(string &url, string &ip, int &port, string &uri) {
	if (0 != memcmp(url.c_str(), "http://", strlen("http://"))) {
		MS_LOG_ERROR("err url:%s", url.c_str());
		return -1;
	}

	string tt = url.substr(strlen("http://"));
	size_t p = tt.find_first_of('/');
	if (p == string::npos) {
		MS_LOG_ERROR("err url:%s", url.c_str());
		return -1;
	}

	uri = tt.substr(p);
	tt = tt.substr(0, p);

	p = tt.find_first_of(':');
	if (p == string::npos) {
		port = 80;
		ip = tt;
	} else {
		ip = tt.substr(0, p);
		port = stoi(tt.substr(p + 1));
	}

	MS_LOG_DEBUG("ip:%s port:%d uri:%s", ip.c_str(), port, uri.c_str());
	return 0;
}

void MsOnvifHandler::gen_digest(string &passwd, string &created, string &nonce, string &digest) {
	string nx1 = GenRandStr(20);

	nonce = EncodeBase64((const unsigned char *)nx1.c_str(), nx1.size());
	created = GmtTimeToStr(time(nullptr));
	created += "Z";

	string cc = nx1 + created + passwd;

	unsigned char xxbuf[20];
	sha1::calc(cc.c_str(), cc.size(), xxbuf);
	digest = EncodeBase64(&xxbuf[0], 20);
}
