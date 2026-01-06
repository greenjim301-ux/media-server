#pragma once

#include <sys/epoll.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <strings.h>
#include <iconv.h>

using MS_SOCKET = int;
using MS_EVENT = epoll_event;

#define MS_INVALID_SOCKET -1
#define MS_MAX_EVENTS 1024
#define MS_FD_READ EPOLLIN
#define MS_FD_CLOSE EPOLLRDHUP
#define MS_FD_ACCEPT EPOLLIN
#define MS_FD_CONNECT EPOLLOUT
#define MS_LAST_ERROR errno

