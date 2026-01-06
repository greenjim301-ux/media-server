#include "MsHttpServer.h"
#include "MsConfig.h"
#include "MsDbMgr.h"
#include "MsDevMgr.h"
#include "MsHttpClient.h"
#include "MsHttpHandler.h"
#include "MsLog.h"
#include "MsPortAllocator.h"
#include <fstream>
#include <thread>

using httpHandle = void (MsHttpServer::*)(shared_ptr<MsEvent>, MsHttpMsg &,
                                          char *, int);

map<int, shared_ptr<SMediaNode>> MsHttpServer::m_mediaNode;

static void GetParam(const char *key, string &value, const string &uri)
{
    const char *p = strstr(uri.c_str(), key);
    if (!p)
    {
        return;
    }

    p += strlen(key);

    if (*p != '=')
    {
        return;
    }

    ++p;
    const char *p2 = p;
    while (*p2 != '&' && *p2 != '\r' && *p2 != '\n' && *p2 != '\0')
    {
        ++p2;
    }

    const char *x = p2 - 1;
    if (*x == '/')
    {
        p2 = x;
    }

    value.assign(p, p2 - p);
}

static void JsonToDev(shared_ptr<MsGbDevice> dev, int type, json &j)
{
    dev->m_deviceID =
        j["deviceId"].is_null() ? "" : j["deviceId"].get<string>();

    dev->m_url = j["url"].is_null() ? "" : j["url"].get<string>();
    dev->m_name = j["name"].get<string>();
    dev->m_domainID = MsConfig::Instance()->GetConfigStr("gbServerID");
    ;
    dev->m_civilCode = j["civilCode"].is_null() ? dev->m_domainID.substr(0, 6)
                                                : j["civilCode"].get<string>();
    dev->m_longitude =
        j["longitude"].is_null() ? "" : j["longitude"].get<string>();
    dev->m_latitude =
        j["latitude"].is_null() ? "" : j["latitude"].get<string>();
    dev->m_user = j["user"].is_null() ? "" : j["user"].get<string>();
    dev->m_pass = j["pass"].is_null() ? "" : j["pass"].get<string>();
    dev->m_ipaddr = j["ipAddr"].is_null() ? "" : j["ipAddr"].get<string>();
    dev->m_port = j["port"].is_null() ? 0 : j["port"].get<int>();
    dev->m_ptzType = j["ptzType"].is_null() ? 0 : j["ptzType"].get<int>();
    dev->m_parentID =
        j["parentId"].is_null() ? "" : j["parentId"].get<string>();
    dev->m_status = "ON";
    dev->m_manufacturer =
        j["manufacturer"].is_null() ? "" : j["manufacturer"].get<string>();
    dev->m_model = j["model"].is_null() ? "" : j["model"].get<string>();
    dev->m_owner = j["owner"].is_null() ? "" : j["owner"].get<string>();
    dev->m_address = j["address"].is_null() ? "" : j["address"].get<string>();
    dev->m_bindIP = j["bindIP"].is_null() ? "" : j["bindIP"].get<string>();
    dev->m_remark = j["remark"].is_null() ? "" : j["remark"].get<string>();
    dev->m_type = type;
}

MsHttpServer::MsHttpServer(int type, int id)
    : MsIHttpServer(type, id) {}

void MsHttpServer::Run()
{
    MsReactor::Run();
    MsConfig *config = MsConfig::Instance();

    string httpIp = config->GetConfigStr("httpIP");
    int httpPort = config->GetConfigInt("httpPort");
    this->OnNodeTimer();

    MsMsg msg;
    msg.m_msgID = MS_MEDIA_NODE_TIMER;
    this->AddTimer(msg, 30, true);

    MsInetAddr bindAddr(AF_INET, httpIp, httpPort);
    shared_ptr<MsSocket> sock = make_shared<MsSocket>(AF_INET, SOCK_STREAM, 0);

    if (0 != sock->Bind(bindAddr))
    {
        MS_LOG_ERROR("http bind %s:%d err:%d", httpIp.c_str(), httpPort,
                     MS_LAST_ERROR);
        this->Exit();
        return;
    }

    if (0 != sock->Listen())
    {
        MS_LOG_ERROR("http listen %s:%d err:%d", httpIp.c_str(), httpPort,
                     MS_LAST_ERROR);
        this->Exit();
        return;
    }

    MS_LOG_INFO("http listen:%s:%d", httpIp.c_str(), httpPort);

    shared_ptr<MsEventHandler> evtHandler = make_shared<MsHttpAcceptHandler>(
        dynamic_pointer_cast<MsIHttpServer>(shared_from_this()));
    shared_ptr<MsEvent> msEvent =
        make_shared<MsEvent>(sock, MS_FD_ACCEPT, evtHandler);

    this->AddEvent(msEvent);

    thread worker(&MsReactor::Wait, shared_from_this());
    worker.detach();
}

void MsHttpServer::QueryPreset(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                               char *body, int len)
{
    try
    {
        json ji = json::parse(body);
        string devId = ji["deviceId"].get<string>();

        shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devId);
        if (!dev.get())
        {
            MS_LOG_WARN("dev:%s not exist", devId.c_str());
            json rsp;
            rsp["code"] = 1;
            rsp["msg"] = "dev not exist";
            SendHttpRsp(evt->GetSocket(), rsp.dump());
        }

        if (dev->m_protocol == GB_DEV)
        {
            MsMsg qr;
            qr.m_msgID = MS_QUERY_PRESET;
            qr.m_strVal = devId;
            qr.m_sessinID = ++m_seqID;
            qr.m_dstType = MS_GB_SERVER;
            qr.m_dstID = 1;
            this->PostMsg(qr);
        }
        else if (dev->m_protocol == RTSP_DEV ||
                 dev->m_protocol == ONVIF_DEV)
        {
            thread ptzx(MsOnvifHandler::QueryPreset, dev->m_user, dev->m_pass,
                        dev->m_owner, dev->m_remark, ++m_seqID);
            ptzx.detach();
        }

        m_evts.emplace(m_seqID, evt);
    }
    catch (json::exception &e)
    {
        MS_LOG_WARN("json err:%s", e.what());
        json rsp;
        rsp["code"] = 1;
        rsp["msg"] = "json error";
        SendHttpRsp(evt->GetSocket(), rsp.dump());
    }
}

void MsHttpServer::QueryRecord(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                               char *body, int len)
{
    try
    {
        json ji = json::parse(body);
        string devId = ji["deviceId"].get<string>();

        auto device = MsDevMgr::Instance()->FindDevice(devId);
        if (!device)
        {
            MS_LOG_WARN("dev:%s not exist", devId.c_str());
            json rsp;
            rsp["code"] = 1;
            rsp["msg"] = "dev not exist";
            SendHttpRsp(evt->GetSocket(), rsp.dump());
            return;
        }

        if (device->m_protocol != GB_DEV)
        {
            MS_LOG_WARN("dev:%s not gb device", devId.c_str());
            json rsp;
            rsp["code"] = 1;
            rsp["msg"] = "dev not gb device";
            SendHttpRsp(evt->GetSocket(), rsp.dump());
            return;
        }

        MsMsg qr;
        qr.m_msgID = MS_INIT_RECORD;
        qr.m_strVal.assign(body, len);
        qr.m_dstType = MS_GB_SERVER;
        qr.m_dstID = 1;
        qr.m_sessinID = ++m_seqID;
        this->PostMsg(qr);
        m_evts.emplace(m_seqID, evt);
    }
    catch (json::exception &e)
    {
        MS_LOG_WARN("json err:%s", e.what());
        json rsp;
        rsp["code"] = 1;
        rsp["msg"] = "json error";
        SendHttpRsp(evt->GetSocket(), rsp.dump());
    }
}

void MsHttpServer::InitCatalog(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                               char *body, int len)
{
    MsMsg bmsg;
    bmsg.m_msgID = MS_HTTP_INIT_CATALOG;
    bmsg.m_dstType = MS_GB_SERVER;
    bmsg.m_dstID = 1;
    this->PostMsg(bmsg);

    json rsp;
    rsp["code"] = 0;
    rsp["msg"] = "success";
    SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::OnNodeTimer()
{
    MS_LOG_DEBUG("node id:%d timer", m_nodeId);
    MsConfig *config = MsConfig::Instance();
    shared_ptr<SMediaNode> mn;
    auto it = m_mediaNode.find(m_nodeId);

    if (it == m_mediaNode.end())
    {
        mn = make_shared<SMediaNode>();
        mn->node_id = m_nodeId;
        mn->m_lastUsed = 0;
        m_mediaNode.emplace(m_nodeId, mn);
    }
    else
    {
        mn = it->second;
    }

    mn->httpStreamPort = config->GetConfigInt("httpStreamPort");
    mn->rtspPort = config->GetConfigInt("rtspPort");
    mn->httpPort = config->GetConfigInt("httpPort");
    mn->httpMediaIP = config->GetConfigStr("httpMediaIP");
    mn->nodeIp = config->GetConfigStr("localBindIP");
    mn->idle = 0;
}

shared_ptr<SMediaNode> MsHttpServer::GetBestMediaNode(const string &devId,
                                                      const string &bindIP)
{
    if (m_mediaNode.size() == 1)
    {
        return m_mediaNode.begin()->second;
    }

    if (bindIP.size())
    {
        for (auto it = m_mediaNode.begin(); it != m_mediaNode.end(); ++it)
        {
            if (it->second->nodeIp == bindIP)
            {
                it->second->m_lastUsed = 1;
                return it->second;
            }
        }
    }

    shared_ptr<SMediaNode> curNode;

    for (auto it = m_mediaNode.begin(); it != m_mediaNode.end(); ++it)
    {
        curNode = it->second;

        if (!curNode->m_lastUsed)
        {
            curNode->m_lastUsed = 1;
            return curNode;
        }
    }

    for (auto it = m_mediaNode.begin(); it != m_mediaNode.end(); ++it)
    {
        it->second->m_lastUsed = 0;
    }

    curNode = m_mediaNode.begin()->second;
    curNode->m_lastUsed = 1;
    return curNode;
}

void MsHttpServer::AddDevice(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                             char *body, int len)
{
    json rsp;

    try
    {
        json j = json::parse(body);
        int pro = j["protocol"].get<int>();

        shared_ptr<MsGbDevice> dev = make_shared<MsGbDevice>(pro);
        JsonToDev(dev, CAMERA_TYPE, j);

        if (pro == RTSP_DEV || pro == RTMP_DEV)
        {
            int ret = MsDevMgr::Instance()->AddCustomDevice(dev);

            rsp["code"] = ret;
            rsp["msg"] = ret ? "add device failed" : "success";

            if (!ret)
            {
                json dd;
                MsDevMgr::Instance()->AssignDev(dev, dd);
                rsp["result"] = dd;
            }
        }
        else if (pro == ONVIF_DEV)
        {
            dev->m_protocol = RTSP_DEV;
            int ret = MsDevMgr::Instance()->AddCustomDevice(dev);

            rsp["code"] = ret;
            rsp["msg"] = ret ? "add device failed" : "success";

            if (ret == 0 && dev->m_user.size() && dev->m_pass.size() &&
                dev->m_ipaddr.size() && dev->m_port > 0)
            {
                MsMsg xm;
                xm.m_msgID = MS_ONVIF_PROBE;
                xm.m_strVal = dev->m_deviceID;
                xm.m_sessinID = ++m_seqID;
                this->EnqueMsg(xm);
                m_evts.emplace(m_seqID, evt);
                return;
            }
            else
            {
                set<string> vv;
                vv.insert(dev->m_deviceID);
                MsDevMgr::Instance()->DeleteDevice(vv);

                rsp["code"] = -1;
                rsp["msg"] = "add device failed";
            }
        }
        else
        {
            rsp["code"] = -1;
            rsp["msg"] = "not support protocol";
        }
    }
    catch (json::exception &e)
    {
        MS_LOG_WARN("json err:%s", e.what());
        rsp["code"] = 1;
        rsp["msg"] = "json error";
    }

    SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::DelDevice(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                             char *body, int len)
{
    json rsp;

    try
    {
        json j = json::parse(body);
        set<string> delDev;

        for (auto &dd : j["device"])
        {
            string devid = dd.get<string>();
            shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devid);

            if (dev)
            {
                delDev.insert(devid);
            }
        }

        MsDevMgr::Instance()->DeleteDevice(delDev);

        rsp["code"] = 0;
        rsp["msg"] = "success";
    }
    catch (json::exception &e)
    {
        MS_LOG_WARN("json err:%s", e.what());

        rsp["code"] = 1;
        rsp["msg"] = "json error";
    }

    SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::GetGbServer(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                               char *body, int len)
{
    json rsp, r;
    MsConfig *f = MsConfig::Instance();

    rsp["code"] = 0;
    rsp["msg"] = "success";

    r["id"] = f->GetConfigStr("gbServerID");
    r["ip"] = f->GetConfigStr("localBindIP");
    r["port"] = f->GetConfigInt("gbServerPort");
    r["pass"] = f->GetConfigStr("gbServerPass");
    r["rtpTransport"] = f->GetConfigInt("rtpTransport");
    rsp["result"] = r;

    SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::OnGenHttpRsp(MsMsg &msg)
{
    auto it = m_evts.find(msg.m_sessinID);

    if (it != m_evts.end())
    {
        shared_ptr<MsEvent> evt = it->second;

        if (msg.m_intVal == 1)
        {
            SendHttpRspEx(evt->GetSocket(), msg.m_strVal);

            m_evts.erase(it);

            this->DelEvent(evt);
        }
        else
        {
            SendHttpRsp(evt->GetSocket(), msg.m_strVal);

            m_evts.erase(it);
        }
    }
}

void MsHttpServer::HandleMsg(MsMsg &msg)
{
    switch (msg.m_msgID)
    {
    case MS_GEN_HTTP_RSP:
        this->OnGenHttpRsp(msg);
        break;

    case MS_MEDIA_NODE_TIMER:
        this->OnNodeTimer();
        break;

    case MS_ONVIF_PROBE:
        this->ProbeOnvif(msg.m_strVal, msg.m_sessinID);
        break;

    case MS_ONVIF_PROBE_TIMEOUT:
    {
        auto it = m_onvif.find(msg.m_strVal);
        if (it != m_onvif.end())
        {
            MS_LOG_INFO("dev:%s probe timeout", msg.m_strVal.c_str());
            shared_ptr<MsOnvifHandler> h = it->second;

            if (h->m_evt.get())
                this->DelEvent(h->m_evt);
            h->m_evt.reset();

            set<string> vv;
            vv.insert(h->m_dev->m_deviceID);
            MsDevMgr::Instance()->DeleteDevice(vv);

            auto ite = m_evts.find(h->m_sid);
            if (ite != m_evts.end())
            {
                json j;
                j["code"] = -1;
                j["msg"] = "onvif probe timeout";
                SendHttpRsp(ite->second->GetSocket(), j.dump());
                m_evts.erase(ite);
            }

            m_onvif.erase(it);
        }
    }
    break;

    case MS_ONVIF_PROBE_FINISH:
    {
        auto it = m_onvif.find(msg.m_strVal);
        if (it != m_onvif.end())
        {
            MS_LOG_INFO("dev:%s probe finish", msg.m_strVal.c_str());
            shared_ptr<MsOnvifHandler> h = it->second;

            auto ite = m_evts.find(h->m_sid);
            if (ite != m_evts.end())
            {
                json j, dd;
                j["code"] = 0;
                j["msg"] = "ok";
                MsDevMgr::Instance()->AssignDev(h->m_dev, dd);
                j["result"] = dd;

                SendHttpRsp(ite->second->GetSocket(), j.dump());
                m_evts.erase(ite);
            }

            m_onvif.erase(it);
        }
    }
    break;

    default:
        MsReactor::HandleMsg(msg);
        break;
    }
}

void MsHttpServer::HandleHttpReq(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                                 char *body, int len)
{
    static map<string, httpHandle> gReqMap = {

        // TODO: check preset and record api
        {"/device", &MsHttpServer::DeviceProcess},
        {"/device/url", &MsHttpServer::GetLiveUrl},
        {"/device/preset", &MsHttpServer::QueryPreset},
        {"/device/ptz", &MsHttpServer::PtzControl},

        {"/gb/server", &MsHttpServer::GetGbServer},
        {"/gb/catalog", &MsHttpServer::InitCatalog},
        {"/gb/domain", &MsHttpServer::GetRegistDomain},
        {"/gb/record", &MsHttpServer::QueryRecord},
        {"/gb/record/url", &MsHttpServer::GetPlaybackUrl},

        {"/sys/node", &MsHttpServer::GetMediaNode},
        {"/sys/config", &MsHttpServer::SetSysConfig},
        {"/sys/netmap", &MsHttpServer::NetMapConfig},
    };

    string uri;
    size_t p = msg.m_uri.find_first_of('?');
    if (p != string::npos)
    {
        uri = msg.m_uri.substr(0, p);
    }
    else
    {
        uri = msg.m_uri;
    }

    auto it = gReqMap.find(uri);

    if (it != gReqMap.end())
    {
        auto func = it->second;
        (this->*func)(evt, msg, body, len);
    }
    else
    {
        MS_LOG_WARN("url:%s not found", msg.m_uri.c_str());
        MsHttpMsg rsp;
        rsp.m_version = msg.m_version;
        rsp.m_status = "404";
        rsp.m_reason = "Not Found";
        rsp.m_connection.SetValue("close");
        SendHttpRsp(evt->GetSocket(), rsp);
        this->DelEvent(evt);
    }
}

void MsHttpServer::DeviceProcess(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len)
{
    if (msg.m_method == "GET")
    {
        this->GetDevList(evt, msg, body, len);
    }
    else if (msg.m_method == "POST")
    {
        this->AddDevice(evt, msg, body, len);
    }
    else if (msg.m_method == "DELETE")
    {
        this->DelDevice(evt, msg, body, len);
    }
    else
    {
        MS_LOG_WARN("unsupported method:%s", msg.m_method.c_str());
        MsHttpMsg rsp;
        rsp.m_version = msg.m_version;
        rsp.m_status = "405";
        rsp.m_reason = "Method Not Allowed";
        rsp.m_connection.SetValue("close");
        SendHttpRsp(evt->GetSocket(), rsp);
    }
}

void MsHttpServer::SetSysConfig(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                                char *body, int len)
{
    json jRsp;

    try
    {
        json j = json::parse(body);

        if (!j["useRAddr"].is_null())
        {
            MsConfig::Instance()->SetConfigInt(
                "useRAddr", j["useRAddr"].get<int>());
        }

        if (j.count("logLevel"))
        {
            int tt = j["logLevel"];
            MsConfig::Instance()->SetConfigInt("logLevel", tt);
            MsLog::Instance()->SetLevel(tt);
        }

        if (j.count("queryRecordType"))
        {
            string tt = j["queryRecordType"];
            MsConfig::Instance()->SetConfigStr("queryRecordType", tt);
        }

        if (j.count("rtpTransport"))
        {
            int tt = j["rtpTransport"];

            if (tt > -1 && tt < 3)
            {
                MsConfig::Instance()->SetConfigInt("rtpTransport", tt);
            }
        }

        if (j.count("httpMediaIP"))
        {
            string tt = j["httpMediaIP"];
            MsConfig::Instance()->SetConfigStr("httpMediaIP", tt);
        }

        if (j.count("gbMediaIP"))
        {
            string tt = j["gbMediaIP"];
            MsConfig::Instance()->SetConfigStr("gbMediaIP", tt);
        }

        if (!j["netType"].is_null())
        {
            MsConfig::Instance()->SetConfigInt("netType",
                                               j["netType"].get<int>());
        }

        if (j.count("detectInterval"))
        {
            MsConfig::Instance()->SetConfigInt("detectInterval",
                                               j["detectInterval"].get<int>());
        }

        jRsp["code"] = 0;
        jRsp["msg"] = "success";
    }
    catch (json::exception &e)
    {
        MS_LOG_WARN("json err:%s", e.what());

        jRsp["code"] = 1;
        jRsp["msg"] = "json error";
    }

    SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::GetLiveUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                              char *body, int len)
{
    json jRsp;
    int nNetType = 0;
    string deviceId, netType;
    GetParam("deviceId", deviceId, msg.m_uri);
    GetParam("netType", netType, msg.m_uri);

    if(netType.size())
    {
        nNetType = atoi(netType.c_str());
    }

    if (deviceId.size() == 0)
    {
        try
        {
            json j = json::parse(body);
            deviceId = j["deviceId"].get<string>();
            nNetType = j["netType"].is_null()
                           ? MsConfig::Instance()->GetConfigInt("netType")
                           : j["netType"].get<int>();
        }
        catch (json::exception &e)
        {
            MS_LOG_WARN("json err:%s", e.what());
            jRsp["code"] = 1;
            jRsp["msg"] = "json error";
            SendHttpRsp(evt->GetSocket(), jRsp.dump());
        }
    }

    shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(deviceId);

    if (!dev.get())
    {
        MS_LOG_ERROR("dev:%s not exist", deviceId.c_str());
        jRsp["code"] = 1;
        jRsp["msg"] = "device not exist";
        return SendHttpRsp(evt->GetSocket(), jRsp.dump());
    }

    shared_ptr<SMediaNode> mn = this->GetBestMediaNode(deviceId, dev->m_bindIP);
    if (!mn.get())
    {
        MS_LOG_ERROR("no media node");
        jRsp["code"] = 1;
        jRsp["msg"] = "no media node";
        return SendHttpRsp(evt->GetSocket(), jRsp.dump());
    }

    string ip = mn->nodeIp;
    json r;
    char bb[512];

    if (nNetType == 1 && mn->httpMediaIP.size())
    {
        ip = mn->httpMediaIP;
    }

    sprintf(bb, "rtsp://%s:%d/live/%s", ip.c_str(), mn->rtspPort,
            deviceId.c_str());
    r["rtspUrl"] = bb;

    sprintf(bb, "http://%s:%d/live/%s.ts", ip.c_str(), mn->httpStreamPort,
            deviceId.c_str());
    r["httpTsUrl"] = bb;

    sprintf(bb, "http://%s:%d/live/%s.flv", ip.c_str(), mn->httpStreamPort,
            deviceId.c_str());
    r["httpFlvUrl"] = bb;

    jRsp["code"] = 0;
    jRsp["msg"] = "success";
    jRsp["result"] = r;
    SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::GetPlaybackUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                                  char *body, int len)
{
    json jRsp;

    try
    {
        json j = json::parse(body);

        string devID = j["deviceId"];
        string st = j["startTime"];
        string et = j["endTime"];
        string type = j["type"];

        int64_t nSt = StrTimeToUnixTime(st);
        int64_t nEt = StrTimeToUnixTime(et);
        int nType = 0;

        nSt -= 8 * 3600;
        nEt -= 8 * 3600;

        if (type == "time")
        {
            nType = 3;
        }
        else if (type == "alarm")
        {
            nType = 2;
        }
        else if (type == "manual")
        {
            nType = 1;
        }
        else if (type == "local")
        {
            nType = 4;
        }

        MsConfig *conf = MsConfig::Instance();
        char bb[512];
        string rid = GenRandStr(16);

        sprintf(bb, "%s-%lld-%lld-%d-%s", devID.c_str(), nSt, nEt, nType,
                rid.c_str());
        string keyId = bb;
        string emptyIP;

        shared_ptr<SMediaNode> mn = this->GetBestMediaNode(keyId, emptyIP);
        if (!mn.get())
        {
            MS_LOG_ERROR("no media node");

            jRsp["code"] = 1;
            jRsp["msg"] = "no media node";

            return SendHttpRsp(evt->GetSocket(), jRsp.dump());
        }

        string ip = mn->nodeIp;
        int netType = j["netType"].is_null()
                          ? MsConfig::Instance()->GetConfigInt("netType")
                          : j["netType"].get<int>();

        if (netType == 1 && mn->httpMediaIP.size())
        {
            ip = mn->httpMediaIP;
        }

        sprintf(bb, "rtsp://%s:%d/gbvod/%s", ip.c_str(), mn->rtspPort,
                keyId.c_str());

        jRsp["rtspUrl"] = bb;

        sprintf(bb, "http://%s:%d/tsvod/%s.ts", ip.c_str(), mn->httpPort,
                keyId.c_str());

        jRsp["tsUrl"] = bb;

        jRsp["httpUrl"] = bb;

        jRsp["code"] = 0;
        jRsp["msg"] = "success";
    }
    catch (json::exception &e)
    {
        MS_LOG_WARN("json err:%s", e.what());

        jRsp["code"] = 1;
        jRsp["msg"] = "json error";
    }

    SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::GetDevList(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                              char *body, int len)
{
    json rsp;
    string deviceId;
    GetParam("deviceId", deviceId, msg.m_uri);

    if (deviceId.size())
    {
        MsDevMgr::Instance()->GetOneDev(rsp, deviceId);
    }
    else
    {
        MsDevMgr::Instance()->GetAllDevice(rsp, 0, 0);
    }

    SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::GetRegistDomain(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                                   char *body, int len)
{
    // send MsMsg to gb server to get regist domain
    MsMsg qr;
    qr.m_sessinID = ++m_seqID;
    qr.m_msgID = MS_GET_REGIST_DOMAIN;
    qr.m_dstType = MS_GB_SERVER;
    qr.m_dstID = 1;
    this->PostMsg(qr);
    m_evts[qr.m_sessinID] = evt;
}

void MsHttpServer::GetMediaNode(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                                char *body, int len)
{
    json j;

    for (auto &nn : m_mediaNode)
    {
        json nd;
        nd["nodeIP"] = nn.second->nodeIp;
        nd["httpPort"] = nn.second->httpPort;
        nd["httpStreamPort"] = nn.second->httpStreamPort;
        nd["rtspPort"] = nn.second->rtspPort;    
        nd["httpMediaIP"] = nn.second->httpMediaIP;  
        j["result"].emplace_back(nd);
    }

    j["code"] = 0;
    j["msg"] = "success";

    SendHttpRsp(evt->GetSocket(), j.dump());
}

void MsHttpServer::PtzControl(shared_ptr<MsEvent> evt, MsHttpMsg &msg,
                              char *body, int len)
{
    json rsp;

    rsp["code"] = 0;
    rsp["msg"] = "ok";

    try
    {
        json j = json::parse(body);
        string deviceId = j["deviceId"].get<string>();
        string presetID =
            j["presetID"].is_null() ? "" : j["presetID"].get<string>();
        int ptzCmd = j["ptzCmd"].get<int>();
        int timeout = 500;

        if (!j["timeout"].is_null())
        {
            timeout = j["timeout"].get<int>();
            if (timeout < 1 || timeout > 10000)
                timeout = 500;
        }

        shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(deviceId);

        if (!dev.get())
        {
            rsp["code"] = 1;
            rsp["msg"] = "dev not exist";
            return SendHttpRsp(evt->GetSocket(), rsp.dump());
        }

        if (ptzCmd < 1 || ptzCmd > 9)
        {
            rsp["code"] = 1;
            rsp["msg"] = "param error";
            return SendHttpRsp(evt->GetSocket(), rsp.dump());
        }

        if (dev->m_protocol == GB_DEV)
        {
            MsMsg qr;
            SPtzCmd *p = new SPtzCmd;

            p->m_devid = deviceId;
            p->m_ptzCmd = ptzCmd;
            p->m_timeout = timeout;
            p->m_presetID = presetID;

            qr.m_msgID = MS_PTZ_CONTROL;
            qr.m_dstType = MS_GB_SERVER;
            qr.m_dstID = 1;
            qr.m_ptr = p;

            this->PostMsg(qr);

            return SendHttpRsp(evt->GetSocket(), rsp.dump());
        }

        if (dev->m_ipaddr.empty() || dev->m_port < 1 || dev->m_user.empty() ||
            dev->m_pass.empty() || dev->m_onvifprofile.empty() ||
            dev->m_onvifptzurl.empty())
        {
            rsp["code"] = 1;
            rsp["msg"] = "param error";
            return SendHttpRsp(evt->GetSocket(), rsp.dump());
        }

        if (dev->m_protocol == RTSP_DEV || dev->m_protocol == ONVIF_DEV)
        {
            thread ptzx(MsOnvifHandler::OnvifPtzControl, dev->m_user, dev->m_pass,
                        dev->m_onvifptzurl, dev->m_onvifprofile, presetID, ptzCmd, timeout);
            ptzx.detach();
        }
    }
    catch (json::exception &e)
    {
        MS_LOG_WARN("json err:%s", e.what());
        rsp["code"] = 1;
        rsp["msg"] = "json error";
    }

    SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::NetMapConfig(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body,
                                int len)
{
    json jRsp;
    jRsp["code"] = 0;
    jRsp["msg"] = "success";

    try
    {
        json j = json::parse(body);
        string cmd = j["cmd"].is_null() ? "get" : j["cmd"].get<string>();

        if (cmd == "get")
        {
            json &obj = MsConfig::Instance()->GetConfigObj();
            jRsp["result"] = obj["netMap"];
        }
        else if (cmd == "add")
        {
            string fromIP = j["fromIP"].get<string>();
            string toIP = j["toIP"].get<string>();
            MsDevMgr::Instance()->AddMapIP(fromIP, toIP);
        }
        else if (cmd == "del")
        {
            string fromIP = j["fromIP"].get<string>();
            MsDevMgr::Instance()->DelMapIP(fromIP);
        }
    }
    catch (json::exception &e)
    {
        json &obj = MsConfig::Instance()->GetConfigObj();
        jRsp["result"] = obj["netMap"];
    }

    SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::ProbeOnvif(string &devid, int sid)
{
    if (m_onvif.count(devid))
    {
        MS_LOG_ERROR("dev:%s probing", devid.c_str());
        return;
    }

    shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devid);
    shared_ptr<MsSocket> udp_sock =
        make_shared<MsSocket>(AF_INET, SOCK_DGRAM, 0);
    MsInetAddr dis_addr(AF_INET, dev->m_ipaddr, 3702);

    string dis_req = "<?xml version=\"1.0\" encoding=\"utf-8\"?><Envelope "
                     "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
                     "xmlns=\"http://www.w3.org/2003/05/"
                     "soap-envelope\"><Header><wsa:MessageID "
                     "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/"
                     "addressing\">uuid:a6f2b7ec-174c-4260-9348-54489e96a05f</"
                     "wsa:MessageID><wsa:To "
                     "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/"
                     "addressing\">urn:schemas-xmlsoap-org:ws:2005:04:"
                     "discovery</wsa:To><wsa:Action "
                     "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/"
                     "addressing\">http://schemas.xmlsoap.org/ws/2005/04/"
                     "discovery/Probe</wsa:Action></Header><Body><Probe "
                     "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                     "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                     "xmlns=\"http://schemas.xmlsoap.org/ws/2005/04/"
                     "discovery\"><Types>tds:Device</Types><Scopes "
                     "/></Probe></Body></Envelope>";

    udp_sock->Sendto(dis_req.c_str(), dis_req.size(), dis_addr);

    shared_ptr<MsOnvifHandler> handler =
        make_shared<MsOnvifHandler>(shared_from_this(), dev, sid);

    shared_ptr<MsEvent> evt = make_shared<MsEvent>(
        udp_sock, MS_FD_READ, dynamic_pointer_cast<MsEventHandler>(handler));

    this->AddEvent(evt);

    handler->m_evt = evt;
    m_onvif[devid] = handler;

    MsMsg tt;
    tt.m_msgID = MS_ONVIF_PROBE_TIMEOUT;
    tt.m_strVal = devid;
    this->AddTimer(tt, 10);
}
