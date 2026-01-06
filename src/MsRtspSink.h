#pragma once
#include "MsResManager.h"
#include "MsRtspMsg.h"
#include "MsLog.h"

class MsIRtspServer
{
public:
    virtual int HandleOptions(MsRtspMsg &msg, shared_ptr<MsEvent> evt)
    {
        MS_LOG_WARN("not implement");
        return -1;
    }
    virtual int HandleDescribe(MsRtspMsg &msg, shared_ptr<MsEvent> evt)
    {
        MS_LOG_WARN("not implement");
        return -1;
    }
    virtual int HandleSetup(MsRtspMsg &msg, shared_ptr<MsEvent> evt)
    {
        MS_LOG_WARN("not implement");
        return -1;
    }
    virtual int HandlePlay(MsRtspMsg &msg, shared_ptr<MsEvent> evt)
    {
        MS_LOG_WARN("not implement");
        return -1;
    }
    virtual int HandleTeardown(MsRtspMsg &msg, shared_ptr<MsEvent> evt)
    {
        MS_LOG_WARN("not implement");
        return -1;
    }
    virtual int HandleOthers(MsRtspMsg &msg, shared_ptr<MsEvent> evt)
    {
        MS_LOG_WARN("not implement");
        return -1;
    }
    virtual int HandlePause(MsRtspMsg &msg, shared_ptr<MsEvent> evt)
    {
        MS_LOG_WARN("not implement");
        return -1;
    }

    virtual void OnWriteEvent(shared_ptr<MsEvent> evt) {};
    virtual void OnCloseEvent(shared_ptr<MsEvent> evt) {};
};

class MsRtspServer : public MsReactor, public MsIRtspServer
{
public:
    using MsReactor::MsReactor;

    void Run() override;

    int HandleOptions(MsRtspMsg &msg, shared_ptr<MsEvent> evt) override;
    int HandleDescribe(MsRtspMsg &msg, shared_ptr<MsEvent> evt) override;

private:
    int m_seqID = 0;
};

class MsRtspSink
    : public MsMeidaSink,
      public MsIRtspServer,
      public enable_shared_from_this<MsRtspSink>
{
public:
    MsRtspSink(const std::string &type, const std::string &streamID, int sinkID,
               std::shared_ptr<MsSocket> sock, MsRtspMsg &descb)
        : MsMeidaSink(type, streamID, sinkID), m_sock(sock), m_descb(descb) {}

    int HandleSetup(MsRtspMsg &msg, shared_ptr<MsEvent> evt) override;
    int HandlePlay(MsRtspMsg &msg, shared_ptr<MsEvent> evt) override;
    int HandleTeardown(MsRtspMsg &msg, shared_ptr<MsEvent> evt) override;
    int HandleOthers(MsRtspMsg &msg, shared_ptr<MsEvent> evt) override;
    int HandlePause(MsRtspMsg &msg, shared_ptr<MsEvent> evt) override;

    void OnStreamInfo(AVStream *video, int videoIdx, AVStream *audio,
                      int audioIdx) override;
    void OnSourceClose() override ;
    void OnStreamPacket(AVPacket *pkt) override;

    void OnWriteEvent(shared_ptr<MsEvent> evt) override;
    void OnCloseEvent(shared_ptr<MsEvent> evt) override;

private:
    int WriteBuffer(const uint8_t *buf, int buf_size, int channel);
    void ReleaseResources();
    void ActiveClose();

private:
    bool m_error = false;
    bool m_playing = false;

    string m_session;
    MsRtspMsg m_descb;
    std::mutex m_queDataMutex;
    std::queue<SData> m_queData;

    // because rtp mux fmt ctx only support one stream,
    // so need two ctx for audio and video
    AVIOContext *m_pb = nullptr;
    AVFormatContext *m_fmtCtx = nullptr;
    AVStream *m_outVideo = nullptr;
    int m_outVideoIdx = 0;

    AVIOContext *m_pbAudio = nullptr;
    AVFormatContext *m_fmtCtxAudio = nullptr;
    AVStream *m_outAudio = nullptr;
    int m_outAudioIdx = 0;

    std::shared_ptr<MsSocket> m_sock;
    shared_ptr<MsReactor> m_reactor;
    shared_ptr<MsEvent> m_evt;
};