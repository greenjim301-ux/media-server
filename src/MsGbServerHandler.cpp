#include "MsGbServerHandler.h"
#include "MsLog.h"
#include "MsMsgDef.h"
#include <string.h>

MsGbServerHandler::MsGbServerHandler(const shared_ptr<MsIGbServer> &server)
    : m_server(server), m_bufSize(DEF_BUF_SIZE), m_bufOff(0) {
	m_bufPtr = make_unique<char[]>(m_bufSize);
}

MsGbServerHandler::~MsGbServerHandler() {}

void MsGbServerHandler::HandleRead(shared_ptr<MsEvent> evt) {
	shared_ptr<MsSocket> sock = evt->GetSharedSocket();
	int recv = 0;
	bool isTcp = sock->IsTcp();

	if (isTcp) {
		recv = sock->Recv(m_bufPtr.get() + m_bufOff, m_bufSize - 1 - m_bufOff);
	} else {
		recv = sock->Recvfrom(m_bufPtr.get() + m_bufOff, m_bufSize - 1 - m_bufOff, m_recvAddr);
	}

	if (recv <= 0) {
		return;
	}

	m_bufOff += recv;
	m_bufPtr[m_bufOff] = '\0';
	int oriTotal = m_bufOff;
	char *p2 = m_bufPtr.get();

	MS_LOG_VERBS("recv %d:\n%s", m_bufOff, p2);

	while (m_bufOff > 0) {
		// bypass \r\n\r\n, which is for NAT keep-alive
		if (m_bufOff >= 4 && strncmp(p2, "\r\n\r\n", 4) == 0) {
			// MS_LOG_DEBUG("gb server skip keep-alive packet");
			p2 += 4;
			m_bufOff -= 4;
			continue;
		}

		// Check for STUN Binding Request (type=0x0001, magic cookie=0x2112A442) and skip it
		if (m_bufOff >= 20 && (unsigned char)p2[0] == 0x00 && (unsigned char)p2[1] == 0x01 &&
		    (unsigned char)p2[4] == 0x21 && (unsigned char)p2[5] == 0x12 &&
		    (unsigned char)p2[6] == 0xA4 && (unsigned char)p2[7] == 0x42) {
			int stunBodyLen = ((unsigned char)p2[2] << 8) | (unsigned char)p2[3];
			int stunTotalLen = 20 + stunBodyLen;
			if (stunTotalLen > m_bufOff) {
				// the whole STUN message has not been received yet, wait for more data
				MS_LOG_DEBUG("gb server incomplete STUN message, need len:%d left:%d", stunTotalLen,
				             m_bufOff);
				return;
			}
			MS_LOG_DEBUG("gb server skip STUN Binding Request, len:%d", stunTotalLen);
			p2 += stunTotalLen;
			m_bufOff -= stunTotalLen;
			continue;
		}

		if (!IsHeaderComplete(p2)) {
			MS_LOG_DEBUG("gb server header not complete: %s", p2);
			// print first 4 bytes of p2 in hex for debugging
			MS_LOG_DEBUG("gb server header not complete first 4 bytes: %02x%02x%02x%02x",
			             (unsigned char)p2[0], (unsigned char)p2[1], (unsigned char)p2[2],
			             (unsigned char)p2[3]);
			if (m_bufOff == m_bufSize - 1) // header too large, reset buf
			{
				MS_LOG_DEBUG("gb server buf full: %s", m_bufPtr.get());
				m_bufOff = 0;
			} else if (m_bufOff) {
				if (p2 != m_bufPtr.get()) {
					MS_LOG_DEBUG("gb server left:%d msg:%s", m_bufOff, p2);
					memmove(m_bufPtr.get(), p2, m_bufOff);
				}
			}
			return;
		}

		char *oriP2 = p2;
		MsSipMsg sipMsg;
		sipMsg.Parse(p2);
		int cntLen = sipMsg.m_contentLength.GetIntVal();
		int left = m_bufOff - (p2 - oriP2);

		if (left < cntLen) {
			p2 = oriP2;
			if (m_bufOff && p2 != m_bufPtr.get()) {
				MS_LOG_DEBUG("gb server need len:%d left:%d cnt:%s", cntLen, left, p2);
				memmove(m_bufPtr.get(), p2, m_bufOff);
			}
			return;
		}

		// MS_LOG_VERBS("recv:\n%s", oriP2);

		if (sipMsg.m_vias.back().HasRport() && !isTcp) {
			sipMsg.m_vias.back().Rebuild(sipMsg.m_vias.back().GetTransport(),
			                             sipMsg.m_vias.back().GetBranch(), m_recvAddr);
		}

		if (sipMsg.m_status.size()) {
			m_server->HandleResponse(sipMsg, sock, p2, cntLen);
		} else if (sipMsg.m_method == "REGISTER") {
			m_server->HandleRegist(sipMsg, sock, m_recvAddr);
		} else if (sipMsg.m_method == "MESSAGE") {
			m_server->HandleMessage(sipMsg, sock, m_recvAddr, p2, cntLen);
		} else if (sipMsg.m_method == "INVITE") {
			m_server->HandleInvite(sipMsg, sock, m_recvAddr, p2, cntLen);
		} else if (sipMsg.m_method == "BYE") {
			m_server->HandleBye(sipMsg, sock, m_recvAddr);
		} else if (sipMsg.m_method == "ACK") {
			m_server->HandleAck(sipMsg);
		} else if (sipMsg.m_method == "NOTIFY") {
			m_server->HandleNotify(sipMsg, sock, m_recvAddr, p2, cntLen);
		} else if (sipMsg.m_method == "CANCEL") {
			m_server->HandleCancel(sipMsg, sock, m_recvAddr);
		} else {
			MS_LOG_WARN("unknown:%s", sipMsg.m_method.c_str());
		}

		p2 += cntLen;
		m_bufOff -= (p2 - oriP2);
	}

	// MS_LOG_DEBUG("gb server all processed, oriTotal:%d left:%d", oriTotal, m_bufOff);
}

void MsGbServerHandler::HandleClose(shared_ptr<MsEvent> evt) {
	MS_LOG_ERROR("gb server handler close");

	m_server->DelEvent(evt);

	MsMsg msg;
	msg.m_msgID = MS_GB_SERVER_HANDLER_CLOSE;
	msg.m_dstType = m_server->GetType();
	msg.m_dstID = m_server->GetID();
	msg.m_intVal = evt->GetSocket()->GetFd();
	m_server->EnqueMsg(msg);
}

void MsGbAcceptHandler::HandleRead(shared_ptr<MsEvent> evt) {
	shared_ptr<MsSocket> sock = evt->GetSharedSocket();
	shared_ptr<MsSocket> clientSock;
	int ret = sock->Accept(clientSock);
	if (ret < 0) {
		MS_LOG_ERROR("gb server accept err:%d", ret);
		return;
	}

	shared_ptr<MsEventHandler> h = make_shared<MsGbServerHandler>(m_server);
	shared_ptr<MsEvent> clientEvent = make_shared<MsEvent>(clientSock, MS_FD_READ | MS_FD_CLOSE, h);
	m_server->AddEvent(clientEvent);
}

void MsGbAcceptHandler::HandleClose(shared_ptr<MsEvent> evt) { m_server->DelEvent(evt); }
