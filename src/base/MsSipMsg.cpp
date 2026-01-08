#include "MsSipMsg.h"
#include "MsLog.h"
#include "MsOsConfig.h"
#include "MsSocket.h"
#include "osip2/osip_md5.h"
#include <string.h>
#include <time.h>

MsSipMsg::MsSipMsg()
    : m_from("From"), m_to("To"), m_callID("Call-ID"), m_cseq("CSeq"), m_contact("Contact"),
      m_maxForwards("Max-Forwards"), m_userAgent("User-Agent"), m_expires("Expires"),
      m_subject("Subject"), m_wwwAuthenticate("WWW-Authenticate"), m_authorization("Authorization"),
      m_date("Date"), m_event("Event"), m_xSource("X-Source") {}

string GenNonce() { return GenRandStr(16); }

void BuildIP(string &uri, const string &ip, int port) {
	uri += ip;
	uri += ':';
	uri += to_string(port);
}

void BuildUri(string &uri, const string &id, const string &ip, int port) {
	uri += "sip:";

	if (id.size()) {
		uri += id;
		uri += '@';
	}

	BuildIP(uri, ip, port);
}

void BuildTo(MsSipFrom &from, const string &id, const string &ip, int port) {
	from.m_value += '<';
	BuildUri(from.m_value, id, ip, port);
	from.m_value += '>';
	from.m_exist = true;
}

void BuildFrom(MsSipFrom &from, const string &id, const string &ip, int port) {
	BuildTo(from, id, ip, port);
	from.AppendTag(GenRandStr(16));
}

void BuildVia(MsComHeader &via, const string &ip, int port) {
	via.m_value = "SIP/2.0/UDP ";
	BuildIP(via.m_value, ip, port);
	via.m_value += ";branch=z9hG4bK";
	via.m_value += GenRandStr(16);
	via.m_exist = true;
}

void BuildCSeq(MsSipCSeq &cseq, int seq, const string &method) {
	cseq.m_value = to_string(seq);
	cseq.m_value += ' ';
	cseq.m_value += method;
	cseq.m_exist = true;
}

void BuildContact(MsSipContact &contact, const string &id, const string &ip, int port) {
	contact.m_value = '<';
	BuildUri(contact.m_value, id, ip, port);
	contact.m_value += '>';
	contact.m_exist = true;
}

void BuildSubject(MsComHeader &subject, const string &sender, const string &recver, bool live) {
	subject.m_value = sender;
	subject.m_value += ':';

	string sid = GenRandStr(16);
	if (live) {
		sid[0] = '0';
	} else {
		sid[0] = '1';
	}

	subject.m_value += sid;
	subject.m_value += ',';
	subject.m_value += recver;
	subject.m_value += ':';
	subject.m_value += GenRandStr(16);
	subject.m_exist = true;
}

void MsSipMsg::Dump(string &rsp) {
	if (m_status.size()) {
		BuildFirstLine(rsp, m_version, m_status, m_reason);
	} else {
		BuildFirstLine(rsp, m_method, m_uri, m_version);
	}

	for (MsSipVia &vv : m_vias) {
		vv.Dump(rsp);
	}

	for (MsComHeader &rr : m_rrs) {
		rr.Dump(rsp);
	}

	m_from.Dump(rsp);
	m_to.Dump(rsp);
	m_callID.Dump(rsp);
	m_cseq.Dump(rsp);
	m_contact.Dump(rsp);
	m_maxForwards.Dump(rsp);
	m_userAgent.Dump(rsp);
	m_expires.Dump(rsp);
	m_contentLength.Dump(rsp);
	m_contentType.Dump(rsp);
	m_subject.Dump(rsp);
	m_wwwAuthenticate.Dump(rsp);
	m_authorization.Dump(rsp);
	m_date.Dump(rsp);
	m_event.Dump(rsp);
	m_xSource.Dump(rsp);

	rsp += "\r\n";

	if (m_body && m_bodyLen) {
		rsp.append(m_body, m_bodyLen);
	}
}

void MsSipMsg::Parse(char *&p2) {
	ParseReqLine(p2, m_method, m_uri, m_version);

	if (m_method == "SIP/2.0") {
		m_status = m_uri;
		m_reason = m_version;
		m_version = m_method;
	}

	string line;
	string key, value;

	while (GetHeaderLine(p2, line)) {
		size_t p1 = line.find_first_of(':');
		size_t p2 = line.find_first_not_of(' ', p1 + 1);

		key = line.substr(0, p1);
		if (p2 != string::npos)
			value = line.substr(p2);
		else
			value = "";

		if (!strcasecmp(key.c_str(), "Via")) {
			MsSipVia via("Via");
			via.SetValue(value);

			m_vias.push_back(via);
		} else if (!strcasecmp(key.c_str(), "Record-Route")) {
			MsComHeader rr("Record-Route");
			rr.SetValue(value);

			m_rrs.push_back(rr);
		} else if (!strcasecmp(key.c_str(), "From")) {
			m_from.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "To")) {
			m_to.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Call-ID")) {
			m_callID.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "CSeq")) {
			m_cseq.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Contact")) {
			m_contact.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Max-Forwards")) {
			m_maxForwards.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Expires")) {
			m_expires.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Content-Length")) {
			m_contentLength.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Content-Type")) {
			m_contentType.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Authorization")) {
			m_authorization.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "Subject")) {
			m_subject.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "WWW-Authenticate")) {
			m_wwwAuthenticate.SetValue(value);
		} else if (!strcasecmp(key.c_str(), "X-Source")) {
			m_xSource.SetValue(value);
		}
	}
}

void MsSipMsg::CloneBasic(MsSipMsg &sipMsg) {
	m_version = sipMsg.m_version;
	m_vias = sipMsg.m_vias;
	m_rrs = sipMsg.m_rrs;
	m_from = sipMsg.m_from;
	m_to = sipMsg.m_to;
	m_cseq = sipMsg.m_cseq;
	m_callID = sipMsg.m_callID;
	m_expires = sipMsg.m_expires;

	m_contentLength.SetIntVal(0);
}

// Contact: <sip:34020000001110000001@192.168.1.237:64098>
string MsSipContact::GetID() {
	size_t p1 = m_value.find("sip:");
	if (p1 == string::npos) {
		return string();
	}

	p1 += 4;
	size_t p2 = m_value.find_first_of('@', p1);
	if (p2 == string::npos) {
		return string();
	}

	return m_value.substr(p1, p2 - p1);
}

string MsSipContact::GetIP() {
	size_t p1 = m_value.find("sip:");
	if (p1 == string::npos) {
		return string();
	}

	p1 += 4;
	size_t p2 = m_value.find_first_of('@', p1);
	if (p2 != string::npos) {
		p1 = p2 + 1;
	}

	p2 = m_value.find_first_of(':', p1);
	if (p2 == string::npos) {
		return string();
	}

	return m_value.substr(p1, p2 - p1);
}

int MsSipContact::GetPort() {
	size_t p1 = m_value.find("sip:");
	if (p1 == string::npos) {
		return 0;
	}

	p1 += 4;
	size_t p2 = m_value.find_first_of('@', p1);
	if (p2 != string::npos) {
		p1 = p2 + 1;
	}

	p2 = m_value.find_first_of(':', p1);
	if (p2 == string::npos) {
		return 0;
	}

	return stoi(m_value.substr(p2 + 1));
}

string MsSipFrom::GetID() {
	size_t p1 = m_value.find("sip:");
	if (p1 == string::npos) {
		return string();
	}

	p1 += 4;
	size_t p2 = m_value.find_first_of('@', p1);
	if (p2 == string::npos) {
		return string();
	}

	return m_value.substr(p1, p2 - p1);
}

string MsSipFrom::GetIP() {
	size_t p1 = m_value.find("sip:");
	if (p1 == string::npos) {
		return string();
	}

	p1 += 4;
	size_t p2 = m_value.find_first_of('@', p1);
	if (p2 != string::npos) {
		p1 = p2 + 1;
	}

	p2 = m_value.find_first_of(':', p1);
	if (p2 == string::npos) {
		return string();
	}

	return m_value.substr(p1, p2 - p1);
}

int MsSipFrom::GetPort() {
	size_t p1 = m_value.find("sip:");
	if (p1 == string::npos) {
		return 0;
	}

	p1 += 4;
	size_t p2 = m_value.find_first_of('@', p1);
	if (p2 != string::npos) {
		p1 = p2 + 1;
	}

	p2 = m_value.find_first_of(':', p1);
	if (p2 == string::npos) {
		return 0;
	}

	return stoi(m_value.substr(p2 + 1));
}

bool MsSipFrom::HasTag() {
	size_t p = m_value.find(";tag=");

	return p != string::npos;
}

void MsSipFrom::AppendTag(string tag) {
	m_value += ";tag=";
	m_value += tag;
}

int MsSipCSeq::GetCSeq() { return atoi(m_value.c_str()); }

string MsSipCSeq::GetMethond() {
	size_t p1 = m_value.find_first_of(' ');
	if (p1 == string::npos) {
		return string();
	}

	size_t p2 = m_value.find_first_not_of(' ', p1);
	if (p2 == string::npos) {
		return string();
	}

	return m_value.substr(p2);
}

void NgxGmtTime(time_t t, struct tm *tp) {
	int32_t yday;
	uint32_t sec, min, hour, mday, mon, year, wday, days, leap;

	/* the calculation is valid for positive time_t only */

	if (t < 0) {
		t = 0;
	}

	days = t / 86400;
	sec = t % 86400;

	/*
	 * no more than 4 year digits supported,
	 * truncate to December 31, 9999, 23:59:59
	 */

	if (days > 2932896) {
		days = 2932896;
		sec = 86399;
	}

	/* January 1, 1970 was Thursday */

	wday = (4 + days) % 7;

	hour = sec / 3600;
	sec %= 3600;
	min = sec / 60;
	sec %= 60;

	/*
	 * the algorithm based on Gauss' formula,
	 * see src/core/ngx_parse_time.c
	 */

	/* days since March 1, 1 BC */
	days = days - (31 + 28) + 719527;

	/*
	 * The "days" should be adjusted to 1 only, however, some March 1st's go
	 * to previous year, so we adjust them to 2.  This causes also shift of the
	 * last February days to next year, but we catch the case when "yday"
	 * becomes negative.
	 */

	year = (days + 2) * 400 / (365 * 400 + 100 - 4 + 1);

	yday = days - (365 * year + year / 4 - year / 100 + year / 400);

	if (yday < 0) {
		leap = (year % 4 == 0) && (year % 100 || (year % 400 == 0));
		yday = 365 + leap + yday;
		year--;
	}

	/*
	 * The empirical formula that maps "yday" to month.
	 * There are at least 10 variants, some of them are:
	 *     mon = (yday + 31) * 15 / 459
	 *     mon = (yday + 31) * 17 / 520
	 *     mon = (yday + 31) * 20 / 612
	 */

	mon = (yday + 31) * 10 / 306;

	/* the Gauss' formula that evaluates days before the month */

	mday = yday - (367 * mon / 12 - 30) + 1;

	if (yday >= 306) {

		year++;
		mon -= 10;

		/*
		 * there is no "yday" in Win32 SYSTEMTIME
		 *
		 * yday -= 306;
		 */
	} else {

		mon += 2;

		/*
		 * there is no "yday" in Win32 SYSTEMTIME
		 *
		 * yday += 31 + 28 + leap;
		 */
	}

	tp->tm_sec = sec;
	tp->tm_min = min;
	tp->tm_hour = hour;
	tp->tm_mday = mday;
	tp->tm_mon = mon;
	tp->tm_year = year;
	tp->tm_wday = wday;
}

string GetTimeStr() {
	char temp[64];
	time_t tt;
	struct tm atm;

	time(&tt);
	NgxGmtTime(tt + 8 * 3600, &atm);

	sprintf(temp, "%04d-%02d-%02dT%02d:%02d:%02d", atm.tm_year, atm.tm_mon, atm.tm_mday,
	        atm.tm_hour, atm.tm_min, atm.tm_sec);

	return string(temp);
}

bool AuthValid(MsComAuth &sipAuth, string &method, string pass) {
	string username = sipAuth.GetAttr("username");
	string realm = sipAuth.GetAttr("realm");
	string uri = sipAuth.GetAttr("uri");
	string response = sipAuth.GetAttr("response");
	string nonce = sipAuth.GetAttr("nonce");

	unsigned char ha1[16];
	unsigned char ha2[16];
	unsigned char res[16];

	unsigned char ha1_hex[32];
	unsigned char ha2_hex[32];
	unsigned char res_hex[32];

	unsigned char c1 = ':';
	osip_MD5_CTX md5_ctx;

	osip_MD5Init(&md5_ctx);
	osip_MD5Update(&md5_ctx, (unsigned char *)username.data(), username.size());
	osip_MD5Update(&md5_ctx, &c1, 1);
	osip_MD5Update(&md5_ctx, (unsigned char *)realm.data(), realm.size());
	osip_MD5Update(&md5_ctx, &c1, 1);
	osip_MD5Update(&md5_ctx, (unsigned char *)pass.data(), pass.size());
	osip_MD5Final(ha1, &md5_ctx);
	CvtToHex(ha1, ha1_hex);

	osip_MD5Init(&md5_ctx);
	osip_MD5Update(&md5_ctx, (unsigned char *)method.data(), method.size());
	osip_MD5Update(&md5_ctx, &c1, 1);
	osip_MD5Update(&md5_ctx, (unsigned char *)uri.data(), uri.size());
	osip_MD5Final(ha2, &md5_ctx);
	CvtToHex(ha2, ha2_hex);

	osip_MD5Init(&md5_ctx);
	osip_MD5Update(&md5_ctx, ha1_hex, 32);
	osip_MD5Update(&md5_ctx, &c1, 1);
	osip_MD5Update(&md5_ctx, (unsigned char *)nonce.data(), nonce.size());
	osip_MD5Update(&md5_ctx, &c1, 1);
	osip_MD5Update(&md5_ctx, ha2_hex, 32);
	osip_MD5Final(res, &md5_ctx);
	CvtToHex(res, res_hex);

	return 0 == memcmp(response.c_str(), res_hex, 32);
}

std::string MsSipVia::get_ip() {
	std::string tt = this->GetTransport();
	const char *p = strchr(tt.c_str(), ':');
	if (!p) {
		return string();
	}

	const char *p1 = p;
	while (*p1 != ' ')
		--p1;

	++p1;

	return string(p1, p - p1);
}

int MsSipVia::get_port() {
	std::string tt = this->GetTransport();
	size_t p1 = tt.find_first_of(':');
	if (p1 == string::npos) {
		return 0;
	}

	return stoi(tt.substr(p1 + 1));
}

std::string MsSipVia::GetTransport() {
	size_t p1 = m_value.find_first_of(';');
	if (p1 == string::npos) {
		return string();
	}

	return m_value.substr(0, p1);
}

std::string MsSipVia::GetBranch() {
	const char *s = m_value.c_str();
	const char *p1 = strstr(s, "branch=");
	if (!p1) {
		return string();
	}

	const char *p2 = p1 + strlen("branch=");
	while (*p2 != '\r' && *p2 != '\n' && *p2 != '\0' && *p2 != ';') {
		++p2;
	}

	return string(p1, p2 - p1);
}

bool MsSipVia::HasRport() {
	size_t p1 = m_value.find("rport");

	return p1 != string::npos;
}

void MsSipVia::Rebuild(const string &transport, const string &branch, MsInetAddr &recvAddr) {
	m_value = transport;
	m_value += ';';
	m_value += branch;
	m_value += ";received=";
	m_value += recvAddr.GetIP();
	m_value += ";rport=";
	m_value += to_string(recvAddr.GetPort());
}

void BuildSipMsg(const string &fromIP, int fromPort, const string &fromID, const string &toIP,
                 int toPort, const string &toID, int cseq, const string &method, MsSipMsg &sipMsg) {
	sipMsg.m_method = method;
	sipMsg.m_version = "SIP/2.0";

	BuildUri(sipMsg.m_uri, toID, toIP, toPort);

	sipMsg.m_vias.clear();
	MsSipVia via("Via");
	BuildVia(via, fromIP, fromPort);
	sipMsg.m_vias.push_back(via);

	BuildFrom(sipMsg.m_from, fromID, fromIP, fromPort);
	BuildTo(sipMsg.m_to, toID, toIP, toPort);
	BuildCSeq(sipMsg.m_cseq, cseq, sipMsg.m_method);

	sipMsg.m_callID.SetValue(GenRandStr(16));
	sipMsg.m_maxForwards.SetIntVal(70);
}

int SendSipMsg(MsSipMsg &msg, shared_ptr<MsSocket> s, string ip, int port) {
	MsInetAddr addr(AF_INET, ip, port);

	return SendSipMsg(msg, s, addr);
}

int SendSipMsg(MsSipMsg &msg, shared_ptr<MsSocket> s, MsInetAddr &addr) {
	string data;
	int ret;
	msg.Dump(data);

	if (s->IsTcp()) {
		ret = s->Send(data.c_str(), data.size());
	} else {
		ret = s->Sendto(data.c_str(), data.size(), addr);
	}

	MS_LOG_INFO("send:%s", data.c_str());

	return ret;
}
