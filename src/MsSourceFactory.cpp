#include "MsSourceFactory.h"
#include "MsDevMgr.h"
#include "MsLog.h"
#include "MsRtspSource.h"
#include "MsFileSource.h"
#include "MsGbSource.h"
#include <fstream>

static int m_seqID = 1;
static std::mutex m_mutex;

std::shared_ptr<MsMediaSource>
MsSourceFactory::CreateMediaSource(const std::string &streamID)
{
    auto device = MsDevMgr::Instance()->FindDevice(streamID);
    if (!device)
    {
        MS_LOG_WARN("device:%s not found", streamID.c_str());
        return nullptr;
    }

    switch (device->m_protocol)
    {
    case RTSP_DEV:
    {
        std::string url = device->m_url;
        if (url.empty())
        {
            MS_LOG_WARN("device:%s has empty url", streamID.c_str());
            return nullptr;
        }

        std::lock_guard<std::mutex> lk(m_mutex);
        return std::make_shared<MsRtspSource>(streamID, url, m_seqID++);
    }
    break;

    case GB_DEV:
    {
        SGbContext *ctx = new SGbContext;
        ctx->gbID = device->m_deviceID;
        ctx->type = 0;
        ctx->startTime = "0";
        ctx->endTime = "0";

        std::lock_guard<std::mutex> lk(m_mutex);
        return std::make_shared<MsGbSource>(streamID, ctx, m_seqID++);
    }
    break;

    default:
        return nullptr;
    }
}

std::shared_ptr<MsMediaSource>
MsSourceFactory::CreateVodSource(const std::string &streamID,
                                 const std::string &filename)
{
    std::string fn = "files/" + filename;

    // Check if file exists
    std::ifstream file(fn);
    if (!file.good())
    {
        MS_LOG_WARN("file not found: %s", fn.c_str());
        return nullptr;
    }

    std::lock_guard<std::mutex> lk(m_mutex);
    return std::make_shared<MsFileSource>(streamID, fn, m_seqID++);
}
