#include "MsHttpClient.h"
#include "nlohmann/json.hpp"
#include "MsConfig.h"
#include "MsLog.h"
#include <thread>

using json = nlohmann::json;

MsHttpClient::MsHttpClient(string &url, const string &value, int type, int id, int msgID)
	: MsReactor(type, id), m_url(url), m_value(value), m_msgID(msgID), m_method("POST")
{
}

void MsHttpClient::Run()
{
	MsReactor::Run();

	thread worker(&MsHttpClient::OnRun, dynamic_pointer_cast<MsHttpClient>(shared_from_this()));
	worker.detach();
}

void MsHttpClient::OnRsp(MsHttpMsg &msg, char *buf, int len)
{
}

class MsHttpClHandler : public MsEventHandler
{
public:
	MsHttpClHandler(const shared_ptr<MsHttpClient> &reactor)
		: m_reactor(reactor)
	{
	}

	void HandleRead(shared_ptr<MsEvent> evt)
	{
		char buf[2048];
		MsSocket *s = evt->GetSocket();

		int r = s->Recv(buf, 2048 - 1);

		if (r <= 0)
		{
			return;
		}

		buf[r] = '\0';
		MS_LOG_DEBUG("recv:%s", buf);

		char *p2 = buf;

		if (IsHeaderComplete(p2))
		{
			char *oriP2 = p2;
			MsHttpMsg msg;

			msg.Parse(p2);

			int cntLen = msg.m_contentLength.GetIntVal();
			int left = r - (p2 - oriP2);

			if (left >= cntLen)
			{
				m_reactor->OnRsp(msg, p2, cntLen);
			}
		}

		m_reactor->PostExit();
	}

	void HandleClose(shared_ptr<MsEvent> evt)
	{
		MS_LOG_DEBUG("http client close");

		m_reactor->DelEvent(evt);
		m_reactor->PostExit();
	}

private:
	shared_ptr<MsHttpClient> m_reactor;
};

void MsHttpClient::OnRun()
{
	if (m_url.size() < strlen("http://"))
	{
		MS_LOG_ERROR("invalid url:%s", m_url.c_str());
		this->Exit();
		return;
	}

	string host, uri, ip;
	string &fp = m_url;
	int iLen = strlen("http://");
	int port;

	size_t p = fp.find_first_of('/', iLen);
	if (p == string::npos)
	{
		MS_LOG_ERROR("invalid url:%s", fp.c_str());
		this->Exit();
		return;
	}

	host = fp.substr(iLen, p - iLen);
	uri = fp.substr(p);

	p = host.find_first_of(':');
	if (p == string::npos)
	{
		ip = host;
		port = 80;
	}
	else
	{
		ip = host.substr(0, p);
		port = stoi(host.substr(p + 1));
	}

	shared_ptr<MsSocket> s = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);

	if (s->Connect(ip, port))
	{
		MS_LOG_WARN("conn failed host:%s", host.c_str());

		this->Exit();

		return;
	}

	MsHttpMsg req;
	string strReq;

	req.m_method = m_method;
	req.m_uri = uri;
	req.m_version = "HTTP/1.1";
	req.m_host.SetValue(host);
	req.m_connection.SetValue("close");
	req.m_contentType.SetValue("application/json; charset=UTF-8");
	req.SetBody(m_value.c_str(), m_value.size());

	req.Dump(strReq);

	int ret = s->Send(strReq.c_str(), strReq.size());

	MS_LOG_DEBUG("ret:%d send:%s", ret, strReq.c_str());

	shared_ptr<MsEventHandler> handler = make_shared<MsHttpClHandler>(
		dynamic_pointer_cast<MsHttpClient>(shared_from_this()));
	shared_ptr<MsEvent> evt = make_shared<MsEvent>(s, MS_FD_READ | MS_FD_CLOSE, handler);

	this->AddEvent(evt);

	MsReactor::Wait();
}
