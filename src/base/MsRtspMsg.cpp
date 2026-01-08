#include "MsRtspMsg.h"
#include "MsLog.h"
#include "MsOsConfig.h"
#include "MsSocket.h"

MsRtspMsg::MsRtspMsg()
    : m_cseq("CSeq"), m_contentBase("Content-Base"), m_public("Public"), m_transport("Transport"),
      m_session("Session"), m_range("Range"), m_rtpInfo("RTP-Info"), m_wwwAuth("WWW-Authenticate"),
      m_auth("Authorization"), m_accept("Accept") {}

void MsRtspMsg::Dump(string &rsp) {
	if (m_status.size()) {
		BuildFirstLine(rsp, m_version, m_status, m_reason);
	} else {
		BuildFirstLine(rsp, m_method, m_uri, m_version);
	}

	m_cseq.Dump(rsp);
	m_public.Dump(rsp);
	m_contentLength.Dump(rsp);
	m_contentType.Dump(rsp);
	m_contentBase.Dump(rsp);
	m_transport.Dump(rsp);
	m_session.Dump(rsp);
	m_range.Dump(rsp);
	m_accept.Dump(rsp);
	m_rtpInfo.Dump(rsp);
	m_auth.Dump(rsp);

	rsp += "\r\n";

	if (m_body && m_bodyLen) {
		rsp.append(m_body, m_bodyLen);
	}
}

void MsRtspMsg::Parse(char *&p2) {
	ParseReqLine(p2, m_method, m_uri, m_version);

	if (m_method == "RTSP/1.0") {
		m_status = m_uri;
		m_reason = m_version;
		m_version = m_method;
	}

	string line;
	string key, value;

	while (GetHeaderLine(p2, line)) {
		size_t p1 = line.find_first_of(':');
		size_t p2 = line.find_first_not_of(' ', p1 + 1);

		if (p2 == string::npos) {
			continue;
		}

		key = line.substr(0, p1);
		value = line.substr(p2);

		if (!strcasecmp(key.c_str(), "CSeq")) {
			m_cseq.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Content-Length")) {
			m_contentLength.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Content-Type")) {
			m_contentType.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Content-Base")) {
			m_contentBase.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Transport")) {
			m_transport.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Session")) {
			m_session.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Range")) {
			m_range.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Public")) {
			m_public.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "WWW-Authenticate")) {
			if (!m_wwwAuth.m_exist) {
				m_wwwAuth.SetValue(value);
			}
		}
	}
}

int SendRtspMsg(MsRtspMsg &msg, MsSocket *s) {
	string data;

	msg.Dump(data);

	int ret = s->Send(data.c_str(), data.size());

	MS_LOG_VERBS("ret:%d send:%s", ret, data.c_str());

	return ret;
}