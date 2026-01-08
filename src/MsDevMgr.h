#pragma once

#include "MsCommon.h"
#include "nlohmann/json.hpp"
#include <map>
#include <mutex>
#include <set>
#include <vector>

using json = nlohmann::json;

enum DEV_PRO {
	UNKNOWN_DEV,
	GB_DEV,
	RTSP_DEV,
	RTMP_DEV,
	ONVIF_DEV,
};

enum MS_DEV_TYPE {
	UNKNOWN_TYPE,
	CAMERA_TYPE,
	CIVIL_TYPE,
	BIZGROUP_TYPE,
	DOMAIN_TYPE,
	VIRTUALGROUP_TYPE,
	NVR_TYPE,
};

class MsGbDevice {
public:
	MsGbDevice(int pro);

	bool m_refreshed;
	int m_ptzType;
	int m_port;
	int m_protocol;
	int m_type;
	string m_codec;
	string m_resolution;
	string m_domainID;
	string m_deviceID;
	string m_parentID;
	string m_name;
	string m_status;
	string m_manufacturer;
	string m_model;
	string m_owner;
	string m_civilCode;
	string m_address;
	string m_ipaddr;
	string m_user;
	string m_pass;
	string m_url;
	string m_longitude;
	string m_latitude;
	string m_bindIP;
	string m_remark;
	string m_onvifprofile;
	string m_onvifptzurl;
};

class ModDev {
public:
	ModDev();

	bool b_bindIP;
	bool b_ptzType;
	int m_port;
	int m_ptzType;
	string m_name;
	string m_address;
	string m_ipaddr;
	string m_user;
	string m_pass;
	string m_longitude;
	string m_latitude;
	string m_civilCode;
	string m_bindIP;
	string m_url;
	string m_owner;
	string m_remark;
	string m_status;
	string m_codec;
	string m_resolution;
	string m_onvifprofile;
	string m_onvifptzurl;
};

class FindDev {
public:
	FindDev();

	string name;
	string domainId;
	string deviceId;
	string bindIP;
	string url;

	int type;
	int ptzType;
	int page;
	int size;
	int protocol;
};

class MsDevMgr {
public:
	MsDevMgr() = default;

	void LoadDevice();
	void AddOrUpdateDevice(shared_ptr<MsGbDevice> dev);
	void DelDeviceInMem(set<string> &delDev);
	void DeleteDevice(set<string> &delDev);
	void AddGroupDev(const string &devId, vector<string> &addDev);
	void DelGroupDev(const string &devId, vector<string> &delDev);
	void GetAllDevice(json &rsp, int page, int size);
	void ModifyDevice(const string &devId, const ModDev &mm);
	void OnDetectResult(const string &devId, const string &c, const string &r);
	void GetOneDev(json &rsp, const string &devId);
	void GetCondiDev(json &rsp, const FindDev &fdev);
	void GetDomainDevice(const string &did, map<string, shared_ptr<MsGbDevice>> &devMap);

	int AddCustomDevice(shared_ptr<MsGbDevice> dev);
	int AddImportedDevcie(shared_ptr<MsGbDevice> dev);
	shared_ptr<MsGbDevice> FindDevice(const string &devId);

	static MsDevMgr *Instance();

	void AssignDev(shared_ptr<MsGbDevice> &dev, json &j);

	static void GetParentID(shared_ptr<MsGbDevice> &device, set<string> &vecParID);

	string GetMapIP(const string &ip);
	void AddMapIP(const string &fromIP, const string &toIP);
	void DelMapIP(const string &fromIP);

private:
	void GetCondiDevInternal(const FindDev &fdev, vector<shared_ptr<MsGbDevice>> &vecDev);
	void WriteMapIP();
	map<string, shared_ptr<MsGbDevice>> m_device;
	map<string, string> m_netMap;

	static unique_ptr<MsDevMgr> m_instance;
	static mutex m_mutex;
};
