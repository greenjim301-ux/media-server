#pragma once
#include "MsResManager.h"

class MsFileSource : public MsMediaSource
{
public:
    MsFileSource(const std::string &streamID, const std::string &filename, int id)
        : MsMediaSource(streamID, MS_FILE_SOURCE, id),
          m_filename(filename) {}

    void Work() override;

private:
    void OnRun();

private:
    int64_t m_lastPts = INT64_MAX;
    int64_t m_lastMs = 0;
    std::string m_filename;
};