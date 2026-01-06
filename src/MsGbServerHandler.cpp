#include "MsGbServerHandler.h"
#include "MsLog.h"
#include <string.h>

MsGbServerHandler::MsGbServerHandler(const shared_ptr<MsIGbServer> &server)
	: m_server(server), m_bufSize(DEF_BUF_SIZE), m_bufOff(0)
{
	m_buf = (char *)malloc(m_bufSize);
}

MsGbServerHandler::~MsGbServerHandler()
{
	free(m_buf);
}

void MsGbServerHandler::HandleRead(shared_ptr<MsEvent> evt)
{
	shared_ptr<MsSocket> sock = evt->GetSharedSocket();
	int recv = 0;
	bool isTcp = sock->IsTcp();

	if (isTcp)
	{
		recv = sock->Recv(m_buf + m_bufOff, m_bufSize - 1 - m_bufOff);
	}
	else
	{
		recv = sock->Recvfrom(m_buf + m_bufOff, m_bufSize - 1 - m_bufOff, m_recvAddr);
	}

	if (recv <= 0)
	{
		return;
	}

	m_bufOff += recv;
	m_buf[m_bufOff] = '\0';
	char *p2 = m_buf;

	while (m_bufOff > 0)
	{
		if (!IsHeaderComplete(p2))
		{
			if (m_bufOff == m_bufSize - 1) // header too large, reset buf
			{
				MS_LOG_DEBUG("gb server buf full: %s", m_buf);
				m_bufOff = 0;
			}
			else if (m_bufOff)
			{
				//MS_LOG_DEBUG("gb server buf left:%d", m_bufOff);
				memmove(m_buf, p2, m_bufOff);
			}
			return;
		}

		char *oriP2 = p2;
		MsSipMsg sipMsg;
		sipMsg.Parse(p2);
		int cntLen = sipMsg.m_contentLength.GetIntVal();
		int left = m_bufOff - (p2 - oriP2);

		if (left < cntLen)
		{
			p2 = oriP2;
			if (m_bufOff)
			{
				MS_LOG_DEBUG("gb server buf left:%d", m_bufOff);
				memmove(m_buf, p2, m_bufOff);
			}
			return;
		}

		MS_LOG_VERBS("recv:%s", m_buf);

		if (sipMsg.m_vias.back().HasRport() && !isTcp)
		{
			sipMsg.m_vias.back().Rebuild(sipMsg.m_vias.back().GetTransport(),
										 sipMsg.m_vias.back().GetBranch(), m_recvAddr);
		}

		if (sipMsg.m_status.size())
		{
			m_server->HandleResponse(sipMsg, sock, p2, cntLen);
		}
		else if (sipMsg.m_method == "REGISTER")
		{
			m_server->HandleRegist(sipMsg, sock, m_recvAddr);
		}
		else if (sipMsg.m_method == "MESSAGE")
		{
			m_server->HandleMessage(sipMsg, sock, m_recvAddr, p2, cntLen);
		}
		else if (sipMsg.m_method == "INVITE")
		{
			m_server->HandleInvite(sipMsg, sock, m_recvAddr, p2, cntLen);
		}
		else if (sipMsg.m_method == "BYE")
		{
			m_server->HandleBye(sipMsg, sock, m_recvAddr);
		}
		else if (sipMsg.m_method == "ACK")
		{
			m_server->HandleAck(sipMsg);
		}
		else if (sipMsg.m_method == "NOTIFY")
		{
			m_server->HandleNotify(sipMsg, sock, m_recvAddr, p2, cntLen);
		}
		else if (sipMsg.m_method == "CANCEL")
		{
			m_server->HandleCancel(sipMsg, sock, m_recvAddr);
		}
		else
		{
			MS_LOG_WARN("unknown:%s", sipMsg.m_method.c_str());
		}

		p2 += cntLen;
		m_bufOff -= (p2 - oriP2);
	}
}

void MsGbServerHandler::HandleClose(shared_ptr<MsEvent> evt)
{
	MS_LOG_ERROR("gb server handler close");

	m_server->DelEvent(evt);

	MsMsg msg;
	msg.m_msgID = MS_GB_SERVER_HANDLER_CLOSE;
	msg.m_dstType = m_server->GetType();
	msg.m_dstID = m_server->GetID();
	msg.m_intVal = evt->GetSocket()->GetFd();
	m_server->EnqueMsg(msg);
}

void MsGbAcceptHandler::HandleRead(shared_ptr<MsEvent> evt)
{
	shared_ptr<MsSocket> sock = evt->GetSharedSocket();
	shared_ptr<MsSocket> clientSock;
	int ret = sock->Accept(clientSock);
	if (ret < 0)
	{
		MS_LOG_ERROR("gb server accept err:%d", ret);
		return;
	}

	shared_ptr<MsEventHandler> h = make_shared<MsGbServerHandler>(m_server);
	shared_ptr<MsEvent> clientEvent = make_shared<MsEvent>(clientSock,
														   MS_FD_READ | MS_FD_CLOSE, h);
	m_server->AddEvent(clientEvent);
}

void MsGbAcceptHandler::HandleClose(shared_ptr<MsEvent> evt)
{
	m_server->DelEvent(evt);
}
