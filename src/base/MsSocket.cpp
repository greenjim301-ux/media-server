#include "MsSocket.h"
#include "MsLog.h"
#include "MsCommon.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

MsSocket::MsSocket(int af, int type, int protocol)
{
	m_sock = socket(af, type, protocol);
	if (m_sock == -1)
	{
		MS_LOG_ERROR("create socker err:%d", errno);
	}
}

MsSocket::MsSocket(MS_SOCKET s)
	: m_sock(s)
{
}

static void MsCloseSocket(MS_SOCKET s)
{
	close(s);
}

MsSocket::~MsSocket()
{
	::MsCloseSocket(m_sock);
}

int MsSocket::Bind(const MsInetAddr &addr)
{
	struct sockaddr_in inAddr;
	socklen_t addrLen = sizeof(inAddr);

	inAddr.sin_family = addr.GetAF();
	inet_pton(addr.GetAF(), addr.GetIP(), &inAddr.sin_addr);
	inAddr.sin_port = htons(addr.GetPort());

	int opt = 1;
	if (setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
	{
		return -1;
	}

	if (bind(m_sock, (struct sockaddr *)&inAddr, addrLen) < 0)
	{
		return -2;
	}

	return 0;
}

int MsSocket::Connect(const MsInetAddr &addr)
{
	struct sockaddr_in inAddr;
	socklen_t addrLen = sizeof(inAddr);

	inAddr.sin_family = addr.GetAF();
	inet_pton(addr.GetAF(), addr.GetIP(), &inAddr.sin_addr);
	inAddr.sin_port = htons(addr.GetPort());

	return connect(m_sock, (struct sockaddr *)&inAddr, addrLen);
}

int MsSocket::Connect(string &ip, int port)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int s;

	/* Obtain address(es) matching host/port */

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;		 /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0; /* Any protocol */

	s = getaddrinfo(ip.c_str(), to_string(port).c_str(), &hints, &result);
	if (s != 0)
	{
		MS_LOG_ERROR("getaddrinfo:%s host:%s", gai_strerror(s), ip.c_str());
		return -1;
	}

	/* getaddrinfo() returns a list of address structures.
		Try each address until we successfully connect(2).
		If socket(2) (or connect(2)) fails, we (close the socket
		and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		this->SetNonBlock();

		if (0 == connect(m_sock, rp->ai_addr, rp->ai_addrlen))
		{
			MS_LOG_INFO("connect ok");
			this->SetBlock();
			break;
		}
		else if (errno == EINPROGRESS)
		{
			int efd = epoll_create(1);
			struct epoll_event ev, evts[2];
			int ee = -1;

			ev.events = EPOLLOUT;
			ev.data.fd = m_sock;

			epoll_ctl(efd, EPOLL_CTL_ADD, m_sock, &ev);

			s = epoll_wait(efd, evts, 2, 10000);
			if (s > 0)
			{
				socklen_t eel = sizeof(int);
				getsockopt(m_sock, SOL_SOCKET, SO_ERROR, (void *)(&ee), &eel);

				if (ee == 0)
				{
					MS_LOG_INFO("connect ok2");
					close(efd);
					this->SetBlock();
					break;
				}
			}

			MS_LOG_ERROR("epoll error:%d sol err:%d", s, ee);

			close(efd);
			close(m_sock);
			m_sock = socket(AF_INET, SOCK_STREAM, 0);
		}
		else
		{
			MS_LOG_ERROR("connect error:%d", errno);
			close(m_sock);
			m_sock = socket(AF_INET, SOCK_STREAM, 0);
		}
	}

	if (rp == NULL)
	{ /* No address succeeded */
		MS_LOG_ERROR("Could not connect %s:%d", ip.c_str(), port);

		freeaddrinfo(result);

		return -1;
	}

	freeaddrinfo(result); /* No longer needed */

	return 0;
}

int MsSocket::Listen()
{
	return listen(m_sock, SOMAXCONN);
}

int MsSocket::Accept(shared_ptr<MsSocket> &rSock)
{
	struct sockaddr_in inAddr;
	socklen_t addrLen = sizeof(inAddr);

	MS_SOCKET s = accept(m_sock, (struct sockaddr *)&inAddr, &addrLen);

	if (s == MS_INVALID_SOCKET)
	{
		return -1;
	}

	rSock = make_shared<MsSocket>(s);

	return 0;
}

int MsSocket::Recv(char *buf, int len)
{
	return recv(m_sock, buf, len, 0);
}

int MsSocket::Recvfrom(char *buf, int len, MsInetAddr &addr)
{
	struct sockaddr_in inAddr;
	socklen_t addrLen = sizeof(inAddr);
	char ipBuf[64] = {0};

	int ret = recvfrom(m_sock, buf, len, 0, (struct sockaddr *)&inAddr, &addrLen);

	if (ret > 0)
	{
		addr.SetAF(inAddr.sin_family);
		addr.SetPort(ntohs(inAddr.sin_port));
		inet_ntop(inAddr.sin_family, &inAddr.sin_addr, ipBuf, 64);
		addr.SetIP(ipBuf);
	}

	return ret;
}

int MsSocket::Send(const char *buf, int len)
{
	int ret;

	while (len)
	{
		ret = send(m_sock, buf, len, 0);
		if (ret < 0)
		{
			MS_LOG_ERROR("send socket err:%d", MS_LAST_ERROR);
			break;
		}
		buf += ret;
		len -= ret;
	}

	return ret < 0 ? ret : len;
}

int MsSocket::Sendto(const char *buf, int len, MsInetAddr &addr)
{
	struct sockaddr_in inAddr;
	int addrLen = sizeof(inAddr);

	inAddr.sin_family = addr.GetAF();
	inet_pton(addr.GetAF(), addr.GetIP(), &inAddr.sin_addr);
	inAddr.sin_port = htons(addr.GetPort());

	return sendto(m_sock, buf, len, 0, (struct sockaddr *)&inAddr, addrLen);
}

void MsSocket::SetNonBlock()
{
	int flags = fcntl(m_sock, F_GETFL, 0);
	fcntl(m_sock, F_SETFL, flags | O_NONBLOCK);
}

void MsSocket::SetBlock()
{
	int flags = fcntl(m_sock, F_GETFL, 0);
	flags = flags & (~O_NONBLOCK);
	fcntl(m_sock, F_SETFL, flags);
}

bool MsSocket::IsTcp()
{
	int type = 0;
	socklen_t len = sizeof(type);

	if (getsockopt(m_sock, SOL_SOCKET, SO_TYPE, (char *)&type, &len) < 0)
	{
		MS_LOG_ERROR("getsockopt err:%d", MS_LAST_ERROR);
		return false;
	}

	return type == SOCK_STREAM;
}

MS_SOCKET MsSocket::GetFd()
{
	return m_sock;
}
