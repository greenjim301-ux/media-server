#pragma once
#include "MsResManager.h"

class MsSourceFactory {
public:
    static std::shared_ptr<MsMediaSource>
    CreateLiveSource(const std::string &streamID);
    
    static std::shared_ptr<MsMediaSource>
    CreateVodSource(const std::string &streamID, const std::string &filename);

    static std::shared_ptr<MsMediaSource>
    CreateGbvodSource(const std::string &streamID, const std::string &streamInfo);
};
