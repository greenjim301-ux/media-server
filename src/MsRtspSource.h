#pragma once
#include "MsResManager.h"

class MsRtspSource : public MsMediaSource
{
public:
    MsRtspSource(const std::string &streamID, const std::string &url, int id)
        : MsMediaSource(streamID, MS_RTSP_SOURCE, id), m_url(url) {}

    void Work() override;

private:
    void OnRun();

private:
    std::string m_url;
};
