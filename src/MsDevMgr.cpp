#include "MsDevMgr.h"
#include "MsConfig.h"
#include "MsDbMgr.h"
#include "MsHttpClient.h"
#include "MsLog.h"
#include "MsReactor.h"
#include "MsPortAllocator.h"

unique_ptr<MsDevMgr> MsDevMgr::m_instance;
mutex MsDevMgr::m_mutex;

static const char *GetDevType(int type)
{
    switch (type)
    {
    case CAMERA_TYPE:
        return "camera";

    case CIVIL_TYPE:
        return "civil";

    case BIZGROUP_TYPE:
        return "bizGroup";

    case VIRTUALGROUP_TYPE:
        return "virtualGroup";

    case NVR_TYPE:
        return "nvr";

    case DOMAIN_TYPE:
        return "domain";

    default:
        return "unknown";
    }
}

void MsDevMgr::GetParentID(shared_ptr<MsGbDevice> &device,
                           set<string> &vecParID)
{
    string &parID = device->m_parentID;

    if (!parID.size())
    {
        return;
    }

    size_t p1 = 0;
    size_t p2;

    while (true)
    {
        p2 = parID.find_first_of('/', p1);

        if (p2 == string::npos)
        {
            vecParID.emplace(parID.substr(p1));
            break;
        }
        else
        {
            vecParID.emplace(parID.substr(p1, p2 - p1));
            p1 = ++p2;
        }
    }
}

static void AssignCameraDev(shared_ptr<MsGbDevice> &dev, json &j)
{
    j["status"] = dev->m_status;
    j["manufacturer"] = dev->m_manufacturer;
    j["model"] = dev->m_model;
    j["owner"] = dev->m_owner;
    j["civilCode"] = dev->m_civilCode;

    j["address"] = dev->m_address;
    j["ipAddr"] = dev->m_ipaddr;
    j["user"] = dev->m_user;
    j["pass"] = dev->m_pass;
    j["port"] = dev->m_port;

    j["longitude"] = dev->m_longitude;
    j["latitude"] = dev->m_latitude;
    j["ptzType"] = dev->m_ptzType;
    j["url"] = dev->m_url;
    j["protocol"] = dev->m_protocol;

    j["codec"] = dev->m_codec;
    j["resolution"] = dev->m_resolution;
    j["bindIP"] = dev->m_bindIP;
    j["remark"] = dev->m_remark;
    j["onvifProfile"] = dev->m_onvifprofile;

    j["onvifPtzUrl"] = dev->m_onvifptzurl;
}

static void AssignBasicDev(shared_ptr<MsGbDevice> &dev, json &j)
{
    j["deviceId"] = dev->m_deviceID;
    j["domainId"] = dev->m_domainID;
    j["name"] = dev->m_name;
    j["type"] = GetDevType(dev->m_type);
}

MsGbDevice::MsGbDevice(int pro)
    : m_status("unknown"),
      m_ptzType(0),
      m_refreshed(true),
      m_port(0),
      m_protocol(pro),
      m_type(UNKNOWN_TYPE),
      m_resolution("unknown"),
      m_codec("unknown")
{
}

void MsDevMgr::AssignDev(shared_ptr<MsGbDevice> &dev, json &j)
{
    AssignBasicDev(dev, j);

    if (dev->m_type == CAMERA_TYPE)
    {
        AssignCameraDev(dev, j);
    }

    set<string> vecParID;
    MsDevMgr::GetParentID(dev, vecParID);

    for (auto &pp : vecParID)
    {
        j["parentId"].emplace_back(pp);
    }
}

static void AssignDbDev(int i, shared_ptr<MsGbDevice> &dev,
                        const string &val)
{
    switch (i)
    {
    case 0:
        dev->m_deviceID = val;
        break;

    case 1:
        dev->m_parentID = val;
        break;

    case 2:
        dev->m_domainID = val;
        break;

    case 3:
        dev->m_name = val;
        break;

    case 4:
        dev->m_status = val;
        break;

    case 5:
        dev->m_manufacturer = val;
        break;

    case 6:
        dev->m_model = val;
        break;

    case 7:
        dev->m_owner = val;
        break;

    case 8:
        dev->m_civilCode = val;
        break;

    case 9:
        dev->m_address = val;
        break;

    case 10:
        dev->m_ipaddr = val;
        break;

    case 11:
        dev->m_user = val;
        break;

    case 12:
        dev->m_pass = val;
        break;

    case 13:
        dev->m_longitude = val;
        break;

    case 14:
        dev->m_latitude = val;
        break;

    case 15:
        dev->m_port = stoi(val);
        break;

    case 16:
        dev->m_url = val;
        break;

    case 17:
        dev->m_ptzType = stoi(val);
        break;

    case 18:
        dev->m_type = stoi(val);
        break;

    case 19:
        dev->m_protocol = stoi(val);
        break;

    case 20:
        dev->m_bindIP = val;
        break;

    case 21:
        dev->m_remark = val;
        break;

    case 22:
        dev->m_codec = val;
        break;

    case 23:
        dev->m_resolution = val;
        break;

    case 24:
        dev->m_onvifprofile = val;
        break;

    case 25:
        dev->m_onvifptzurl = val;
        break;

    default:
        break;
    }
}

void MsDevMgr::LoadDevice()
{
    char sql[512];
    int nSql = sprintf(sql, "select \
          device_id, parent_id, domain_id,  name, status, \
          manufacturer, model, owner, civil_code, address, \
          ip_addr, user, pass, longitude, latitude, \
          port, url, ptz_type, type, protocol, \
          bind_ip, remark, codec, resolution, onvif_profile, \
          onvif_ptz_url from device");

    sqlite3 *pSql = MsDbMgr::Instance()->GetSql();
    sqlite3_stmt *pStmt = NULL;

    int rc = sqlite3_prepare_v2(pSql, sql, nSql, &pStmt, NULL);

    if (pStmt)
    {
        while (SQLITE_ROW == sqlite3_step(pStmt))
        {
            int num = sqlite3_column_count(pStmt);
            shared_ptr<MsGbDevice> dev = make_shared<MsGbDevice>(UNKNOWN_DEV);

            for (int i = 0; i < num; i++)
            {
                string val = (const char *)sqlite3_column_text(pStmt, i);
                AssignDbDev(i, dev, val);
            }

            m_device.emplace(dev->m_deviceID, dev);
        }

        sqlite3_finalize(pStmt);
    }

    MsDbMgr::Instance()->RelSql();

    // load net map here, a little ugly
    json &obj = MsConfig::Instance()->GetConfigObj();
    for (auto &nn : obj["netMap"])
    {
        string fromIP = nn["fromIP"].get<string>();
        string toIP = nn["toIP"].get<string>();
        m_netMap[fromIP] = toIP;
    }
}

void MsDevMgr::AddOrUpdateDevice(shared_ptr<MsGbDevice> dev)
{
    lock_guard<mutex> lk(MsDevMgr::m_mutex);
    auto it = m_device.find(dev->m_deviceID);

    if (it == m_device.end())
    {
        m_device.emplace(dev->m_deviceID, dev);
        this->AddImportedDevcie(dev);
    }
    else
    {
        shared_ptr<MsGbDevice> gb = it->second;
        string sql = "update device set ";

        if (gb->m_name != dev->m_name)
        {
            gb->m_name = dev->m_name;

            sql += "name='";
            sql += dev->m_name;
            sql += "' ,";
        }

        if (gb->m_status != dev->m_status)
        {
            gb->m_status = dev->m_status;

            sql += "status='";
            sql += dev->m_status;
            sql += "' ,";
        }

        if (gb->m_type != dev->m_type)
        {
            gb->m_type = dev->m_type;

            sql += "type=";
            sql += to_string(dev->m_type);
            sql += " ,";
        }

        if (gb->m_longitude != dev->m_longitude && dev->m_longitude.size())
        {
            gb->m_longitude = dev->m_longitude;

            sql += "longitude='";
            sql += dev->m_longitude;
            sql += "' ,";
        }

        if (gb->m_latitude != dev->m_latitude && dev->m_latitude.size())
        {
            gb->m_latitude = dev->m_latitude;

            sql += "latitude='";
            sql += dev->m_latitude;
            sql += "' ,";
        }

        if (sql.size() > strlen("update device set "))
        {
            sql.resize(sql.size() - 1);
            sql += "where device_id='";
            sql += dev->m_deviceID;
            sql += "'";

            sqlite3 *pSql = MsDbMgr::Instance()->GetSql();
            char *zErrMsg = NULL;
            int rc = sqlite3_exec(pSql, sql.c_str(), NULL, 0, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                MS_LOG_ERROR("update device err:%s", zErrMsg);
                sqlite3_free(zErrMsg);
            }

            MsDbMgr::Instance()->RelSql();
        }
    }
}

int MsDevMgr::AddCustomDevice(shared_ptr<MsGbDevice> dev)
{
    bool genid = true;

    if (dev->m_deviceID.size())
    {
        genid = false;
    }
    else
    {
        dev->m_deviceID = "1";
    }

    if (dev->m_parentID.size() == 0)
    {
        string sDomainID = MsConfig::Instance()->GetConfigStr("gbServerID");
        dev->m_parentID = sDomainID;
    }

    // here only insert 22 fields
    char sql[1024];
    int nSql = sprintf(
        sql, "insert into device ( \
          device_id, parent_id, domain_id,  name, status, \
          manufacturer, model, owner, civil_code, address, \
          ip_addr, user, pass, longitude, latitude, \
          port, url, ptz_type, type, protocol, \
		  bind_ip, remark, onvif_profile, onvif_ptz_url) \
values ('%s', '%s','%s', '%s', '%s', \
        '%s', '%s','%s', '%s', '%s', \
        '%s', '%s','%s', '%s', '%s', \
         %d, '%s', %d, %d, %d, \
         '%s', '%s')",
        dev->m_deviceID.c_str(), dev->m_parentID.c_str(), dev->m_domainID.c_str(), dev->m_name.c_str(), dev->m_status.c_str(),
        dev->m_manufacturer.c_str(), dev->m_model.c_str(), dev->m_owner.c_str(), dev->m_civilCode.c_str(), dev->m_address.c_str(),
        dev->m_ipaddr.c_str(), dev->m_user.c_str(), dev->m_pass.c_str(), dev->m_longitude.c_str(), dev->m_latitude.c_str(),
        dev->m_port, dev->m_url.c_str(), dev->m_ptzType, dev->m_type, dev->m_protocol,
        dev->m_bindIP.c_str(), dev->m_remark.c_str());

    sqlite3 *pSql = MsDbMgr::Instance()->GetSql();
    char *zErrMsg = NULL;

    int rc = sqlite3_exec(pSql, sql, NULL, 0, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        MS_LOG_ERROR("insert device:%s err:%s", dev->m_name.c_str(), zErrMsg);
        sqlite3_free(zErrMsg);
        MsDbMgr::Instance()->RelSql();
        return -1;
    }

    if (genid)
    {
        int64_t devId = sqlite3_last_insert_rowid(pSql);
        char ss[32];
        if (dev->m_type == CAMERA_TYPE)
        {
            sprintf(ss, "%s131%07lld", dev->m_domainID.substr(0, 10).c_str(),
                    devId);
        }
        else if (dev->m_type == BIZGROUP_TYPE)
        {
            sprintf(ss, "%s216%07lld", dev->m_domainID.substr(0, 10).c_str(),
                    devId);
        }
        else
        {
            MS_LOG_ERROR("error dev type");
            MsDbMgr::Instance()->RelSql();
            return -1;
        }

        dev->m_deviceID = ss;

        nSql = sprintf(sql, "update device set device_id='%s' where id=%lld",
                       dev->m_deviceID.c_str(), devId);

        rc = sqlite3_exec(pSql, sql, NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            MS_LOG_ERROR("update device:%s err:%s", dev->m_name.c_str(),
                         zErrMsg);
            sqlite3_free(zErrMsg);
            MsDbMgr::Instance()->RelSql();
            return -1;
        }
    }

    MsDbMgr::Instance()->RelSql();

    lock_guard<mutex> lk(MsDevMgr::m_mutex);
    m_device[dev->m_deviceID] = dev;

    return 0;
}

int MsDevMgr::AddImportedDevcie(shared_ptr<MsGbDevice> dev)
{
    if (dev->m_parentID.size() == 0)
    {
        dev->m_parentID = dev->m_domainID;
    }

    char sql[1024];
    int nSql = 0;

    nSql = sprintf(
        sql, "insert into device ( \
        device_id, parent_id, domain_id,  name, status, \
        manufacturer, model, owner, civil_code, address, \
        ip_addr, user, pass, longitude, latitude, \
        port, url, ptz_type, type, protocol, \
		bind_ip, remark) \
values ('%s', '%s','%s', '%s', 'OFF', \
    '%s', '%s','%s', '%s', '%s', \
    '%s', '%s','%s', '%s', '%s', \
        %d, '%s', %d, %d, %d, \
        '%s', '%s')",
        dev->m_deviceID.c_str(), dev->m_parentID.c_str(),
        dev->m_domainID.c_str(), dev->m_name.c_str(),
        dev->m_manufacturer.c_str(), dev->m_model.c_str(), dev->m_owner.c_str(),
        dev->m_civilCode.c_str(), dev->m_address.c_str(), dev->m_ipaddr.c_str(),
        dev->m_user.c_str(), dev->m_pass.c_str(), dev->m_longitude.c_str(),
        dev->m_latitude.c_str(), dev->m_port, dev->m_url.c_str(),
        dev->m_ptzType, dev->m_type, dev->m_protocol, dev->m_bindIP.c_str(),
        dev->m_remark.c_str());

    sqlite3 *pSql = MsDbMgr::Instance()->GetSql();
    char *zErrMsg = NULL;

    int rc = sqlite3_exec(pSql, sql, NULL, 0, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        MS_LOG_ERROR("insert device:%s err:%s", dev->m_name.c_str(), zErrMsg);
        sqlite3_free(zErrMsg);
        MsDbMgr::Instance()->RelSql();
        return -1;
    }

    MsDbMgr::Instance()->RelSql();

    return 0;
}

void MsDevMgr::DelDeviceInMem(set<string> &delDev)
{
    if (delDev.empty())
    {
        return;
    }

    {
        lock_guard<mutex> lk(MsDevMgr::m_mutex);
        for (auto &dd : delDev)
        {
            m_device.erase(dd);
        }
    }
}

void MsDevMgr::DeleteDevice(set<string> &delDev)
{
    char sql[256];
    sqlite3 *pSql = MsDbMgr::Instance()->GetSql();

    for (auto &dd : delDev)
    {
        int nSql = sprintf(sql, "delete from device where device_id='%s'", dd.c_str());
        char *zErrMsg = NULL;

        int rc = sqlite3_exec(pSql, sql, NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            MS_LOG_ERROR("delete device err:%s", zErrMsg);
            sqlite3_free(zErrMsg);
        }

        nSql = sprintf(sql, "delete from group_dev where device_id='%s'", dd.c_str());

        rc = sqlite3_exec(pSql, sql, NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            MS_LOG_ERROR("delete group_dev err:%s", zErrMsg);
            sqlite3_free(zErrMsg);
        }
    }

    MsDbMgr::Instance()->RelSql();

    this->DelDeviceInMem(delDev);
}

void MsDevMgr::AddGroupDev(const string &devId, vector<string> &addDev)
{
    char sql[512];
    int nSql;
    shared_ptr<MsGbDevice> dev;

    for (auto &dd : addDev)
    {
        sqlite3 *pSql = MsDbMgr::Instance()->GetSql();
        nSql = sprintf(sql,
                       "insert into group_dev (group_id, device_id, name, "
                       "type) values ('%s', '%s', '%s', %d)",
                       devId.c_str(), dd.c_str(), dev->m_name.c_str(),
                       dev->m_type);

        char *zErrMsg = NULL;
        int rc = sqlite3_exec(pSql, sql, NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            MS_LOG_ERROR("insert group_dev err:%s", zErrMsg);
            sqlite3_free(zErrMsg);
        }

        MsDbMgr::Instance()->RelSql();
    }
}

void MsDevMgr::DelGroupDev(const string &devId, vector<string> &delDev)
{
    char sql[512];
    int nSql;

    for (auto &dd : delDev)
    {
        sqlite3 *pSql = MsDbMgr::Instance()->GetSql();
        nSql = sprintf(
            sql, "delete from group_dev where group_id='%s' and device_id='%s'",
            devId.c_str(), dd.c_str());

        char *zErrMsg = NULL;
        int rc = sqlite3_exec(pSql, sql, NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            MS_LOG_ERROR("delete group_dev err:%s", zErrMsg);
            sqlite3_free(zErrMsg);
        }

        MsDbMgr::Instance()->RelSql();
    }
}

shared_ptr<MsGbDevice> MsDevMgr::FindDevice(const string &devId)
{

    lock_guard<mutex> lk(m_mutex);
    auto it = m_device.find(devId);
    if (it != m_device.end())
    {
        return it->second;
    }
    else
    {
        return nullptr;
    }
}

MsDevMgr *MsDevMgr::Instance()
{
    if (MsDevMgr::m_instance.get())
    {
        return MsDevMgr::m_instance.get();
    }
    else
    {
        lock_guard<mutex> lk(MsDevMgr::m_mutex);

        if (MsDevMgr::m_instance.get())
        {
            return MsDevMgr::m_instance.get();
        }
        else
        {
            MsDevMgr::m_instance = make_unique<MsDevMgr>();
            return MsDevMgr::m_instance.get();
        }
    }
}

void MsDevMgr::GetAllDevice(json &jRsp, int page, int size)
{
    jRsp["code"] = 0;
    jRsp["msg"] = "success";

    vector<shared_ptr<MsGbDevice>> devices;

    {
        lock_guard<mutex> lk(m_mutex);

        if (page > 0 && size > 0)
        {
            int off = (page - 1) * size;
            int mm = page * size;
            int nn = 0;

            for (auto &dd : m_device)
            {
                ++nn;

                if (nn > off && nn <= mm)
                {
                    devices.emplace_back(dd.second);
                }
                else if (nn > mm)
                {
                    break;
                }
            }
        }
        else
        {
            for (auto &dd : m_device)
            {
                devices.emplace_back(dd.second);
            }
        }
    }

    for (auto &dd : devices)
    {
        json j;
        AssignDev(dd, j);
        jRsp["result"].emplace_back(j);
    }
}

void MsDevMgr::ModifyDevice(const string &devId, const ModDev &mm)
{
    lock_guard<mutex> lk(m_mutex);
    auto it = m_device.find(devId);
    if (it == m_device.end())
    {
        return;
    }

    shared_ptr<MsGbDevice> dev = it->second;
    string sql = "update device set ";

    if (mm.m_port > 0)
    {
        dev->m_port = mm.m_port;
        sql += "port=";
        sql += to_string(mm.m_port);
        sql += " ,";
    }

    if (mm.m_ipaddr.size())
    {
        dev->m_ipaddr = mm.m_ipaddr;
        sql += "ip_addr='";
        sql += mm.m_ipaddr;
        sql += "' ,";
    }

    if (mm.m_codec.size() && mm.m_codec != dev->m_codec)
    {
        dev->m_codec = mm.m_codec;
        sql += "codec='";
        sql += mm.m_codec;
        sql += "' ,";
    }

    if (mm.m_resolution.size() && mm.m_resolution != dev->m_resolution)
    {
        dev->m_resolution = mm.m_resolution;
        sql += "resolution='";
        sql += mm.m_resolution;
        sql += "' ,";
    }

    if (mm.m_status.size() && mm.m_status != dev->m_status)
    {
        dev->m_status = mm.m_status;
        sql += "status='";
        sql += mm.m_status;
        sql += "' ,";
    }

    if (mm.m_name.size())
    {
        dev->m_name = mm.m_name;
        sql += "name='";
        sql += mm.m_name;
        sql += "' ,";
    }

    if (mm.m_address.size())
    {
        dev->m_address = mm.m_address;
        sql += "address='";
        sql += mm.m_address;
        sql += "' ,";
    }

    if (mm.m_user.size())
    {
        dev->m_user = mm.m_user;
        sql += "user='";
        sql += mm.m_user;
        sql += "' ,";
    }

    if (mm.m_pass.size())
    {
        dev->m_pass = mm.m_pass;
        sql += "pass='";
        sql += mm.m_pass;
        sql += "' ,";
    }

    if (mm.m_civilCode.size())
    {
        dev->m_civilCode = mm.m_civilCode;
        sql += "civil_code='";
        sql += mm.m_civilCode;
        sql += "' ,";
    }

    if (mm.m_latitude.size())
    {
        dev->m_latitude = mm.m_latitude;
        sql += "latitude='";
        sql += mm.m_latitude;
        sql += "' ,";
    }

    if (mm.m_longitude.size())
    {
        dev->m_longitude = mm.m_longitude;
        sql += "longitude='";
        sql += mm.m_longitude;
        sql += "' ,";
    }

    if (mm.m_url.size())
    {
        dev->m_url = mm.m_url;
        sql += "url='";
        sql += mm.m_url;
        sql += "' ,";
    }

    if (mm.m_remark.size())
    {
        dev->m_remark = mm.m_remark;
        sql += "remark='";
        sql += mm.m_remark;
        sql += "' ,";
    }

    if (mm.m_owner.size())
    {
        dev->m_owner = mm.m_owner;
        sql += "owner='";
        sql += mm.m_owner;
        sql += "' ,";
    }

    if (mm.b_bindIP)
    {
        dev->m_bindIP = mm.m_bindIP;
        sql += "bind_ip='";
        sql += mm.m_bindIP;
        sql += "' ,";
    }

    if (mm.b_ptzType)
    {
        dev->m_ptzType = mm.m_ptzType;
        sql += "ptz_type=";
        sql += to_string(mm.m_ptzType);
        sql += " ,";
    }

    if (mm.m_onvifprofile.size())
    {
        dev->m_onvifprofile = mm.m_onvifprofile;
        sql += "onvif_profile='";
        sql += mm.m_onvifprofile;
        sql += "' ,";
    }

    if (mm.m_onvifptzurl.size())
    {
        dev->m_onvifptzurl = mm.m_onvifptzurl;
        sql += "onvif_ptz_url='";
        sql += mm.m_onvifptzurl;
        sql += "' ,";
    }

    sql.resize(sql.size() - 1);
    sql += "where device_id='";
    sql += devId;
    sql += "'";

    sqlite3 *pSql = MsDbMgr::Instance()->GetSql();

    char *zErrMsg = NULL;
    int rc = sqlite3_exec(pSql, sql.c_str(), NULL, 0, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        MS_LOG_ERROR("mod device err:%s", zErrMsg);
        sqlite3_free(zErrMsg);
    }

    MsDbMgr::Instance()->RelSql();
}

void MsDevMgr::OnDetectResult(const string &devId, const string &c,
                              const string &r)
{
    bool pull = c.size() && r.size();

    ModDev mod;
    mod.m_status = pull ? "ON" : "OFF";
    mod.m_codec = c.size() ? c : "unknown";
    mod.m_resolution = r.size() ? r : "unknown";
    this->ModifyDevice(devId, mod);
}

void MsDevMgr::GetOneDev(json &rsp, const string &devId)
{
    shared_ptr<MsGbDevice> dev;

    {
        lock_guard<mutex> lk(m_mutex);
        auto it = m_device.find(devId);
        if (it != m_device.end())
        {
            dev = it->second;
        }
    }

    if (dev.get())
    {
        json j;

        AssignDev(dev, j);

        rsp["result"] = j;
        rsp["code"] = 0;
        rsp["msg"] = "success";
    }
    else
    {
        rsp["code"] = 1;
        rsp["msg"] = "device not exist";
    }
}

void MsDevMgr::GetCondiDevInternal(const FindDev &fdev,
                                   vector<shared_ptr<MsGbDevice>> &vecDev)
{
    const string &domainId = fdev.domainId;
    const string &url = fdev.url;
    const string &bindIP = fdev.bindIP;
    const string &name = fdev.name;
    const string &devId = fdev.deviceId;
    const int &type = fdev.type;

    const int &ptzType = fdev.ptzType;
    const int &protocol = fdev.protocol;

    {
        lock_guard<mutex> lk(m_mutex);

        for (auto &&dd : m_device)
        {
            shared_ptr<MsGbDevice> &dev = dd.second;

            if (type)
            {
                if (dev->m_type != type)
                {
                    continue;
                }
            }

            if (domainId.size())
            {
                if (dev->m_domainID != domainId)
                {
                    continue;
                }
            }

            if (devId.size())
            {
                if (string::npos == dev->m_deviceID.find(devId))
                {
                    continue;
                }
            }

            if (bindIP.size())
            {
                if (dev->m_bindIP != bindIP)
                {
                    continue;
                }
            }

            if (name.size())
            {
                if (string::npos == dev->m_name.find(name))
                {
                    continue;
                }
            }

            if (url.size())
            {
                if (string::npos == dev->m_url.find(url))
                {
                    continue;
                }
            }

            if (ptzType)
            {
                if (dev->m_ptzType != ptzType)
                {
                    continue;
                }
            }

            if (protocol)
            {
                if (dev->m_protocol != protocol)
                {
                    continue;
                }
            }

            vecDev.push_back(dev);
        }
    }
}

void MsDevMgr::GetCondiDev(json &jRsp, const FindDev &fdev)
{
    vector<shared_ptr<MsGbDevice>> vecDev;
    this->GetCondiDevInternal(fdev, vecDev);
    const int &page = fdev.page;
    const int &size = fdev.size;

    jRsp["code"] = 0;
    jRsp["msg"] = "success";
    jRsp["totalSize"] = vecDev.size();

    if (page > 0 && size > 0)
    {
        int off = (page - 1) * size;
        int mm = page * size;
        int nn = 0;

        for (auto &dd : vecDev)
        {
            ++nn;

            if (nn > off && nn <= mm)
            {
                json j;
                AssignDev(dd, j);
                jRsp["result"].emplace_back(j);
            }
            else if (nn > mm)
            {
                break;
            }
        }
    }
    else
    {
        for (auto &dd : vecDev)
        {
            json j;
            AssignDev(dd, j);
            jRsp["result"].emplace_back(j);
        }
    }
}

void MsDevMgr::GetDomainDevice(const string &did, map<string, shared_ptr<MsGbDevice>> &devMap)
{
    lock_guard<mutex> lk(m_mutex);
    for (auto &&dd : m_device)
    {
        shared_ptr<MsGbDevice> &dev = dd.second;
        if (dev->m_domainID == did)
        {
            devMap.emplace(dev->m_deviceID, dev);
        }
    }
}

std::string MsDevMgr::GetMapIP(const string &ip)
{
    auto it = m_netMap.find(ip);
    if (it == m_netMap.end())
    {
        return string();
    }
    else
    {
        return it->second;
    }
}

void MsDevMgr::AddMapIP(const string &fromIP, const string &toIP)
{
    m_netMap[fromIP] = toIP;
    this->WriteMapIP();
}

void MsDevMgr::DelMapIP(const string &fromIP)
{
    m_netMap.erase(fromIP);
    this->WriteMapIP();
}

void MsDevMgr::WriteMapIP()
{
    json &obj = MsConfig::Instance()->GetConfigObj();
    obj["netMap"] = json::array();

    for (auto &dd : m_netMap)
    {
        json j;
        j["fromIP"] = dd.first;
        j["toIP"] = dd.second;
        obj["netMap"].emplace_back(j);
    }

    MsConfig::Instance()->WriteConfig();
}

ModDev::ModDev() : b_bindIP(false), b_ptzType(false), m_port(0), m_ptzType(0) {}

FindDev::FindDev()
    : type(CAMERA_TYPE), ptzType(0), page(0), size(0), protocol(0) {}
