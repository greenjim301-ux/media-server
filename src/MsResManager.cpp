#include "MsResManager.h"
#include <algorithm>
#include <thread>

// Static member initialization
std::mutex MsResManager::m_instanceMutex;
MsResManager *MsResManager::m_instance = nullptr;

MsResManager &MsResManager::GetInstance()
{
    if (m_instance == nullptr)
    {
        std::lock_guard<std::mutex> lock(m_instanceMutex);
        if (m_instance == nullptr)
        {
            m_instance = new MsResManager();
        }
    }
    return *m_instance;
}

void MsResManager::AddMediaSource(const std::string &key,
                                  std::shared_ptr<MsMediaSource> source)
{
    if (key.empty() || !source)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mapMutex);
    m_mediaSources[key] = source;
}

void MsResManager::RemoveMediaSource(const std::string &key)
{
    if (key.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mapMutex);
    m_mediaSources.erase(key);
}

std::shared_ptr<MsMediaSource>
MsResManager::GetMediaSource(const std::string &key)
{
    if (key.empty())
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_mapMutex);
    auto it = m_mediaSources.find(key);
    if (it != m_mediaSources.end())
    {
        return it->second;
    }
    return nullptr;
}

void MsMediaSource::AddSink(std::shared_ptr<MsMeidaSink> sink)
{
    if (!sink || m_isClosing.load())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_sinkMutex);
    if (m_sinks.empty())
    {
        MsResManager::GetInstance().AddMediaSource(
            m_streamID, std::dynamic_pointer_cast<MsMediaSource>(shared_from_this()));
    }

    m_sinks.push_back(sink);
    if (m_video || m_audio)
    {
        sink->OnStreamInfo(m_video, m_videoIdx, m_audio, m_audioIdx);
    }
}

void MsMediaSource::RemoveSink(const std::string &type, int sinkID)
{
    std::unique_lock<std::mutex> lock(m_sinkMutex);
    this->RemoveSinkNoLock(type, sinkID);
}

void MsMediaSource::RemoveSinkNoLock(const std::string &type, int sinkID)
{
    for (auto it = m_sinks.begin(); it != m_sinks.end(); ++it)
    {
        if ((*it)->m_type == type && (*it)->m_sinkID == sinkID)
        {
            m_sinks.erase(it);
            break;
        }
    }

    if (m_sinks.empty())
    {
        this->OnSinksEmpty();
    }
}

void MsMediaSource::NotifyStreamInfo()
{
    std::lock_guard<std::mutex> lock(m_sinkMutex);
    for (auto &sink : m_sinks)
    {
        if (sink)
        {
            sink->OnStreamInfo(m_video, m_videoIdx, m_audio, m_audioIdx);
        }
    }
}

void MsMediaSource::NotifySourceClose()
{
    std::lock_guard<std::mutex> lock(m_sinkMutex);
    for (auto &sink : m_sinks)
    {
        if (sink)
        {
            sink->OnSourceClose();
        }
    }
    m_sinks.clear();
}

void MsMediaSource::NotifyStreamPacket(AVPacket *pkt)
{
    std::lock_guard<std::mutex> lock(m_sinkMutex);
    for (auto &sink : m_sinks)
    {
        if (sink)
        {
            sink->OnStreamPacket(pkt);
        }
    }
    if (m_sinks.empty())
    {
        this->OnSinksEmpty();
    }
}

void MsMediaSource::ActiveClose()
{
    m_isClosing.store(true);
    MsResManager::GetInstance().RemoveMediaSource(m_streamID);
    this->NotifySourceClose();

    this->PostExit();
}

void MsMediaSource::OnSinksEmpty()
{
    m_isClosing.store(true);
    this->PostExit();
}

void MsMediaSource::Work()
{
    MsReactor::Run();
    std::thread reactorWorker([reactor = shared_from_this()]()
                              { reactor->Wait(); });
    reactorWorker.detach();
}

void MsMeidaSink::DetachSource()
{
    auto source = MsResManager::GetInstance().GetMediaSource(m_streamID);
    if (source)
        source->RemoveSink(m_type, m_sinkID);
}

void MsMeidaSink::DetachSourceNoLock()
{
    auto source = MsResManager::GetInstance().GetMediaSource(m_streamID);
    if (source)
        source->RemoveSinkNoLock(m_type, m_sinkID);
}

void MsMeidaSink::OnStreamInfo(AVStream *video, int videoIdx, AVStream *audio,
                               int audioIdx)
{
    m_video = video;
    m_videoIdx = videoIdx;
    m_audio = audio;
    m_audioIdx = audioIdx;
}
