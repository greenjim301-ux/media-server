#include "MsHttpHandler.h"
#include "MsEvent.h"
#include "MsHttpServer.h"
#include "MsLog.h"
#include "MsCommon.h"
#include <string.h>
#include "nlohmann/json.hpp"
#include <map>

using json = nlohmann::json;

MsHttpAcceptHandler::MsHttpAcceptHandler(const shared_ptr<MsIHttpServer> &server)
	: m_server(server)
{
}

void MsHttpAcceptHandler::HandleRead(shared_ptr<MsEvent> evt)
{
	MsSocket *sock = evt->GetSocket();
	shared_ptr<MsSocket> s;

	if (sock->Accept(s))
	{
		MS_LOG_WARN("accept err:%d", MS_LAST_ERROR);
		return;
	}

	shared_ptr<MsEventHandler> evtHandler = make_shared<MsHttpHandler>(m_server);
	shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(s, MS_FD_READ | MS_FD_CLOSE, evtHandler);
	m_server->AddEvent(msEvent);
}

void MsHttpAcceptHandler::HandleClose(shared_ptr<MsEvent> evt)
{
	m_server->DelEvent(evt);
}

MsHttpHandler::MsHttpHandler(const shared_ptr<MsIHttpServer> &server)
	: m_server(server), m_bufSize(DEF_BUF_SIZE), m_bufOff(0)
{
	m_buf = (char *)malloc(m_bufSize);
}

MsHttpHandler::~MsHttpHandler()
{
	free(m_buf);
}

void MsHttpHandler::HandleRead(shared_ptr<MsEvent> evt)
{
	MsSocket *sock = evt->GetSocket();

	int recv = sock->Recv(m_buf + m_bufOff, m_bufSize - 1 - m_bufOff);

	if (recv <= 0)
	{
		return;
	}

	m_bufOff += recv;
	m_buf[m_bufOff] = '\0';

	char *p2 = m_buf;

	while (m_bufOff)
	{
		if (!IsHeaderComplete(p2))
		{
			if (m_bufOff)
			{
				if (m_bufOff > 16384)
				{
					MS_LOG_ERROR("left buf over 16k, reset");
					m_bufOff = 0;
				}
				else if (p2 != m_buf)
				{
					MS_LOG_DEBUG("http buf left:%d", m_bufOff);
					memmove(m_buf, p2, m_bufOff);
				}
			}

			return;
		}

		MS_LOG_DEBUG("recv:%s", m_buf);

		char *oriP2 = p2;
		MsHttpMsg msg;

		msg.Parse(p2);

		int cntLen = msg.m_contentLength.GetIntVal();
		int left = m_bufOff - (p2 - oriP2);

		if (left < cntLen)
		{
			int headrLen = p2 - oriP2;
			p2 = oriP2;

			if (m_bufOff && p2 != m_buf)
			{
				MS_LOG_DEBUG("http buf left:%d", m_bufOff);
				memmove(m_buf, p2, m_bufOff);
			}

			if (headrLen + cntLen >= m_bufSize)
			{
				int newBufSize = headrLen + cntLen + 1024;
				char *newBuf = (char *)malloc(newBufSize);
				memcpy(newBuf, m_buf, m_bufOff);

				printf("extend mem %d:%d\n", m_bufSize, newBufSize);

				free(m_buf);
				m_bufSize = newBufSize;
				m_buf = newBuf;
			}

			return;
		}

		m_server->HandleHttpReq(evt, msg, p2, cntLen);

		p2 += cntLen;
		m_bufOff -= (p2 - oriP2);
	}

	if (m_bufSize > DEF_BUF_SIZE)
	{
		printf("relase big mem\n");

		free(m_buf);
		m_bufSize = DEF_BUF_SIZE;
		m_buf = (char *)malloc(DEF_BUF_SIZE);
	}
}

void MsHttpHandler::HandleClose(shared_ptr<MsEvent> evt)
{
	m_server->DelEvent(evt);
}
