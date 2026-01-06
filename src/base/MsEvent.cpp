#include "MsEvent.h"
#include "MsLog.h"

MsEvent::MsEvent(
	shared_ptr<MsSocket>& sock, 
	int eventMask,
	const shared_ptr<MsEventHandler>& handler)
	: m_handler(handler)
	, m_sock(sock)
{

	m_event.events = eventMask;
	
}

MsEvent::~MsEvent()
{

}

MS_EVENT* MsEvent::GetEvent()
{
	return &m_event;
}

void MsEvent::SetEvent(int eventMask)
{
	m_event.events = eventMask;
}

void MsEvent::SetHandler(shared_ptr<MsEventHandler> handler)
{
	m_handler = handler;
}

MsSocket* MsEvent::GetSocket()
{
	return m_sock.get();
}

shared_ptr<MsSocket> MsEvent::GetSharedSocket()
{
	return m_sock;
}

void MsEvent::HandleEvent(uint32_t eventMask)
{
	if (eventMask & MS_FD_READ)
	{
		m_handler->HandleRead(shared_from_this());
	}

	if (eventMask & EPOLLOUT)
	{
		m_handler->HandleWrite(shared_from_this());
    }

	if (eventMask & MS_FD_CLOSE
		|| eventMask & EPOLLERR
		|| eventMask & EPOLLHUP)
	{
		m_handler->HandleClose(shared_from_this());
	}

}
