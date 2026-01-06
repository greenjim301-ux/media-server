#pragma once
#include "MsEvent.h"
#include "MsHttpMsg.h"
#include "MsSocket.h"
#include "MsReactor.h"

class MsIHttpServer : public MsReactor {
public:
    using MsReactor::MsReactor;
    virtual void HandleHttpReq(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                               char *body, int len) = 0;
};

class MsHttpAcceptHandler : public MsEventHandler {
public:
    MsHttpAcceptHandler(const shared_ptr<MsIHttpServer> &server);

    void HandleRead(shared_ptr<MsEvent> evt);
    void HandleClose(shared_ptr<MsEvent> evt);

private:
    shared_ptr<MsIHttpServer> m_server;
};

class MsHttpHandler : public MsEventHandler {
public:
    MsHttpHandler(const shared_ptr<MsIHttpServer> &server);
    ~MsHttpHandler();

    void HandleRead(shared_ptr<MsEvent> evt);
    void HandleClose(shared_ptr<MsEvent> evt);

private:
    shared_ptr<MsIHttpServer> m_server;

    char *m_buf;
    int m_bufSize;
    int m_bufOff;
};
