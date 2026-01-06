#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "MsReactor.h"
#include "MsLog.h"

extern "C"
{
#include <libavformat/avformat.h>
}

class MsMeidaSink
{
public:
    MsMeidaSink(const std::string &type, const std::string &streamID,
                int sinkID)
        : m_type(type), m_streamID(streamID), m_sinkID(sinkID) {}
    virtual ~MsMeidaSink() { MS_LOG_INFO("media sink %s-%d destroyed",
                                         m_type.c_str(), m_sinkID); }

    virtual void DetachSource();
    virtual void DetachSourceNoLock();

    virtual void OnStreamInfo(AVStream *video, int videoIdx, AVStream *audio,
                              int audioIdx);

    virtual void OnSourceClose() = 0;
    virtual void OnStreamPacket(AVPacket *pkt) = 0;

public:
    std::string m_type;
    std::string m_streamID;
    int m_sinkID;

protected:
    AVStream *m_video = nullptr;
    int m_videoIdx = -1;
    AVStream *m_audio = nullptr;
    int m_audioIdx = -1;
};

class MsMediaSource : public MsReactor
{
public:
    MsMediaSource(const std::string &streamID, int type, int id)
        : MsReactor(type, id),
          m_streamID(streamID) {}

    virtual ~MsMediaSource()
    {
        MS_LOG_INFO("media source %s destroyed", m_streamID.c_str());
    }

    void AddSink(std::shared_ptr<MsMeidaSink> sink);
    void RemoveSink(const std::string &type, int sinkID);
    void RemoveSinkNoLock(const std::string &type, int sinkID);
    void NotifyStreamInfo();
    void NotifySourceClose();
    void NotifyStreamPacket(AVPacket *pkt);
    virtual void ActiveClose();
    virtual void OnSinksEmpty();
    virtual void Work();

protected:
    std::atomic_bool m_isClosing{false};
    AVStream *m_video = nullptr;
    int m_videoIdx = -1;
    AVStream *m_audio = nullptr;
    int m_audioIdx = -1;
    std::string m_streamID;
    std::mutex m_sinkMutex;
    std::vector<std::shared_ptr<MsMeidaSink>> m_sinks;
};

class MsResManager
{
public:
    static MsResManager &GetInstance();

    void AddMediaSource(const std::string &key,
                        std::shared_ptr<MsMediaSource> source);
    void RemoveMediaSource(const std::string &key);
    std::shared_ptr<MsMediaSource> GetMediaSource(const std::string &key);

private:
    MsResManager() = default;
    ~MsResManager() = default;

    MsResManager(const MsResManager &) = delete;
    MsResManager &operator=(const MsResManager &) = delete;

    static std::mutex m_instanceMutex;
    static MsResManager *m_instance;

    std::mutex m_mapMutex;
    std::map<std::string, std::shared_ptr<MsMediaSource>> m_mediaSources;
};