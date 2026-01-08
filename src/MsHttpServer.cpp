#include "MsHttpServer.h"
#include "MsConfig.h"
#include "MsDbMgr.h"
#include "MsDevMgr.h"
#include "MsHttpHandler.h"
#include "MsLog.h"
#include "MsPortAllocator.h"
#include <fstream>
#include <thread>

extern "C" {
#include "libavformat/avformat.h"
}

using httpHandle = void (MsHttpServer::*)(shared_ptr<MsEvent>, MsHttpMsg &, char *, int);

map<int, shared_ptr<SMediaNode>> MsHttpServer::m_mediaNode;

static void GetParam(const char *key, string &value, const string &uri) {
	const char *p = strstr(uri.c_str(), key);
	if (!p) {
		return;
	}

	p += strlen(key);

	if (*p != '=') {
		return;
	}

	++p;
	const char *p2 = p;
	while (*p2 != '&' && *p2 != '\r' && *p2 != '\n' && *p2 != '\0') {
		++p2;
	}

	const char *x = p2 - 1;
	if (*x == '/') {
		p2 = x;
	}

	value.assign(p, p2 - p);
}

static void JsonToDev(shared_ptr<MsGbDevice> dev, int type, json &j) {
	dev->m_deviceID = j["deviceId"].is_null() ? "" : j["deviceId"].get<string>();

	dev->m_url = j["url"].is_null() ? "" : j["url"].get<string>();
	dev->m_name = j["name"].get<string>();
	dev->m_domainID = MsConfig::Instance()->GetConfigStr("gbServerID");
	;
	dev->m_civilCode =
	    j["civilCode"].is_null() ? dev->m_domainID.substr(0, 6) : j["civilCode"].get<string>();
	dev->m_longitude = j["longitude"].is_null() ? "" : j["longitude"].get<string>();
	dev->m_latitude = j["latitude"].is_null() ? "" : j["latitude"].get<string>();
	dev->m_user = j["user"].is_null() ? "" : j["user"].get<string>();
	dev->m_pass = j["pass"].is_null() ? "" : j["pass"].get<string>();
	dev->m_ipaddr = j["ipAddr"].is_null() ? "" : j["ipAddr"].get<string>();
	dev->m_port = j["port"].is_null() ? 0 : j["port"].get<int>();
	dev->m_ptzType = j["ptzType"].is_null() ? 0 : j["ptzType"].get<int>();
	dev->m_parentID = j["parentId"].is_null() ? "" : j["parentId"].get<string>();
	dev->m_status = "ON";
	dev->m_manufacturer = j["manufacturer"].is_null() ? "" : j["manufacturer"].get<string>();
	dev->m_model = j["model"].is_null() ? "" : j["model"].get<string>();
	dev->m_owner = j["owner"].is_null() ? "" : j["owner"].get<string>();
	dev->m_address = j["address"].is_null() ? "" : j["address"].get<string>();
	dev->m_bindIP = j["bindIP"].is_null() ? "" : j["bindIP"].get<string>();
	dev->m_remark = j["remark"].is_null() ? "" : j["remark"].get<string>();
	dev->m_type = type;
}

MsHttpServer::MsHttpServer(int type, int id) : MsIHttpServer(type, id) {}

void MsHttpServer::Run() {
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

	if (0 != sock->Bind(bindAddr)) {
		MS_LOG_ERROR("http bind %s:%d err:%d", httpIp.c_str(), httpPort, MS_LAST_ERROR);
		this->Exit();
		return;
	}

	if (0 != sock->Listen()) {
		MS_LOG_ERROR("http listen %s:%d err:%d", httpIp.c_str(), httpPort, MS_LAST_ERROR);
		this->Exit();
		return;
	}

	MS_LOG_INFO("http listen:%s:%d", httpIp.c_str(), httpPort);

	shared_ptr<MsEventHandler> evtHandler =
	    make_shared<MsHttpAcceptHandler>(dynamic_pointer_cast<MsIHttpServer>(shared_from_this()));
	shared_ptr<MsEvent> msEvent = make_shared<MsEvent>(sock, MS_FD_ACCEPT, evtHandler);

	this->AddEvent(msEvent);

	thread worker(&MsReactor::Wait, shared_from_this());
	worker.detach();
}

void MsHttpServer::QueryPreset(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	try {
		json ji = json::parse(body);
		string devId = ji["deviceId"].get<string>();

		shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devId);
		if (!dev.get()) {
			MS_LOG_WARN("dev:%s not exist", devId.c_str());
			json rsp;
			rsp["code"] = 1;
			rsp["msg"] = "dev not exist";
			SendHttpRsp(evt->GetSocket(), rsp.dump());
		}

		if (dev->m_protocol == GB_DEV) {
			MsMsg qr;
			qr.m_msgID = MS_QUERY_PRESET;
			qr.m_strVal = devId;
			qr.m_sessinID = ++m_seqID;
			qr.m_dstType = MS_GB_SERVER;
			qr.m_dstID = 1;
			this->PostMsg(qr);
		} else if ((dev->m_protocol == RTSP_DEV || dev->m_protocol == ONVIF_DEV) &&
		           dev->m_onvifptzurl.size() && dev->m_onvifprofile.size()) {
			thread ptzx(MsOnvifHandler::QueryPreset, dev->m_user, dev->m_pass, dev->m_onvifptzurl,
			            dev->m_onvifprofile, ++m_seqID);
			ptzx.detach();
		} else {
			MS_LOG_WARN("dev:%s not support preset query", devId.c_str());
			json rsp;
			rsp["code"] = 1;
			rsp["msg"] = "dev not support preset query";
			SendHttpRsp(evt->GetSocket(), rsp.dump());
			return;
		}

		m_evts.emplace(m_seqID, evt);
	} catch (json::exception &e) {
		MS_LOG_WARN("json err:%s", e.what());
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "json error";
		SendHttpRsp(evt->GetSocket(), rsp.dump());
	}
}

void MsHttpServer::FileUpload(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	if (msg.m_method != "POST") {
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "method not allowed";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());
		return;
	}

	std::string contentType = msg.m_contentType.m_value;
	std::string boundary_prefix = "boundary=";
	size_t b_pos = contentType.find(boundary_prefix);
	if (b_pos == std::string::npos) {
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "boundary not found";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());
		return;
	}

	std::string boundary = "--" + contentType.substr(b_pos + boundary_prefix.length());

	auto find_mem = [](const char *haystack, int hlen, const char *needle, int nlen) -> int {
		if (hlen < nlen)
			return -1;
		for (int i = 0; i <= hlen - nlen; ++i) {
			if (memcmp(haystack + i, needle, nlen) == 0)
				return i;
		}
		return -1;
	};

	int start_pos = find_mem(body, len, boundary.c_str(), boundary.length());
	if (start_pos == -1) {
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "invalid body";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());
		return;
	}

	int curr_pos = start_pos;
	std::vector<std::string> filenames;

	while (true) {
		curr_pos += boundary.length();
		if (curr_pos + 2 > len)
			break;

		if (memcmp(body + curr_pos, "--", 2) == 0)
			break;
		if (memcmp(body + curr_pos, "\r\n", 2) != 0)
			break;

		curr_pos += 2; // skip \r\n

		int header_end = find_mem(body + curr_pos, len - curr_pos, "\r\n\r\n", 4);
		if (header_end == -1)
			break;
		header_end += curr_pos;

		std::string headers(body + curr_pos, header_end - curr_pos);
		int content_start = header_end + 4;

		int next_boundary = find_mem(body + content_start, len - content_start, boundary.c_str(),
		                             boundary.length());
		if (next_boundary == -1)
			break;
		next_boundary += content_start;

		int content_end = next_boundary - 2; // remove preceding \r\n
		int content_len = content_end - content_start;

		std::string filename;
		std::string disp_prefix = "Content-Disposition:";
		size_t disp_pos = headers.find(disp_prefix);
		if (disp_pos != std::string::npos) {
			size_t fn_pos = headers.find("filename=\"", disp_pos);
			if (fn_pos != std::string::npos) {
				fn_pos += 10;
				size_t fn_end = headers.find("\"", fn_pos);
				if (fn_end != std::string::npos) {
					filename = headers.substr(fn_pos, fn_end - fn_pos);
				}
			}
		}

		if (!filename.empty() && content_len > 0) {
			std::ofstream ofs("files/" + filename, std::ios::binary);
			if (ofs.is_open()) {
				ofs.write(body + content_start, content_len);
				ofs.close();
				filenames.push_back(filename);
			}
		}

		curr_pos = next_boundary;
	}

	if (filenames.size() > 1) {
		// too many, can only handle one, return error and delete files
		for (const auto &fname : filenames) {
			std::remove(("files/" + fname).c_str());
		}
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "only one file allowed";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());
		return;
	}

	AVFormatContext *fmt_ctx = nullptr;
	std::string file_path = "files/" + filenames[0];
	int ret = avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr);
	if (ret < 0) {
		std::remove(file_path.c_str());
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "open file failed";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());
		return;
	}

	if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
		avformat_close_input(&fmt_ctx);
		std::remove(file_path.c_str());
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "find stream info failed";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());
		return;
	}

	string codec;
	string resolution;
	double frame_rate = 0.0;

	for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
		if (fmt_ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264 ||
		    fmt_ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_HEVC) {
			codec = (fmt_ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) ? "h264" : "h265";
			if (fmt_ctx->streams[i]->avg_frame_rate.den != 0) {
				frame_rate = av_q2d(fmt_ctx->streams[i]->avg_frame_rate);
			}
			resolution = std::to_string(fmt_ctx->streams[i]->codecpar->width) + "x" +
			             std::to_string(fmt_ctx->streams[i]->codecpar->height);
			break;
		}
	}

	if (fmt_ctx->duration <= 0 || frame_rate <= 0.0) {
		avformat_close_input(&fmt_ctx);
		std::remove(file_path.c_str());
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "invalid format";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());
		return;
	}

	double duration_sec = fmt_ctx->duration * av_q2d(AV_TIME_BASE_Q);
	avformat_close_input(&fmt_ctx);

	std::ifstream in(file_path, std::ios::ate | std::ios::binary);
	long long fsize = 0;
	if (in.is_open()) {
		fsize = in.tellg();
		in.close();
	}

	char *zErrMsg = 0;
	auto pSql = MsDbMgr::Instance()->GetSql();
	std::string sql =
	    "INSERT INTO t_file (name, size, codec, res, duration, frame_rate) VALUES ('" +
	    filenames[0] + "', " + std::to_string(fsize) + ", '" + codec + "', '" + resolution + "', " +
	    std::to_string(duration_sec) + ", " + std::to_string(frame_rate) + ")";

	sqlite3_exec(pSql, sql.c_str(), NULL, 0, &zErrMsg);
	if (zErrMsg) {
		MS_LOG_ERROR("insert t_file error: %s", zErrMsg);
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "database error";
		SendHttpRspEx(evt->GetSocket(), rsp.dump());

		sqlite3_free(zErrMsg);
		MsDbMgr::Instance()->RelSql();
		return;
	}

	// get last insert id
	int64_t file_id = sqlite3_last_insert_rowid(pSql);
	MsDbMgr::Instance()->RelSql();

	json r, rsp;
	r["fileId"] = file_id;
	r["fileName"] = filenames[0];
	r["size"] = fsize;
	r["codec"] = codec;
	r["resolution"] = resolution;
	r["duration"] = duration_sec;
	r["frameRate"] = frame_rate;
	rsp["code"] = 0;
	rsp["msg"] = "success";
	rsp["result"] = r;
	SendHttpRspEx(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::FileProcess(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	if (msg.m_method == "DELETE") {
		try {
			json ji = json::parse(body);
			int64_t fileId = ji["fileId"].get<int64_t>();

			char *zErrMsg = 0;
			auto pSql = MsDbMgr::Instance()->GetSql();
			// select file name
			std::string sql = "SELECT name FROM t_file WHERE file_id = " + std::to_string(fileId);
			sqlite3_stmt *stmt;
			int rc = sqlite3_prepare_v2(pSql, sql.c_str(), -1, &stmt, NULL);
			if (rc == SQLITE_OK) {
				rc = sqlite3_step(stmt);
				if (rc == SQLITE_ROW) {
					std::string filename = reinterpret_cast<const char *>(
					    sqlite3_column_text(stmt, 0)); // delete file from disk
					std::string filepath = "files/" + filename;
					if (std::remove(filepath.c_str()) != 0) {
						MS_LOG_WARN("delete file:%s error", filepath.c_str());
					}
				}
				sqlite3_finalize(stmt);
			}

			// delete record from db
			sql = "DELETE FROM t_file WHERE file_id = " + std::to_string(fileId);
			sqlite3_exec(pSql, sql.c_str(), NULL, 0, &zErrMsg);
			if (zErrMsg) {
				MS_LOG_ERROR("delete t_file error: %s", zErrMsg);
				sqlite3_free(zErrMsg);
			}

			MsDbMgr::Instance()->RelSql();

			json rsp;
			rsp["code"] = 0;
			rsp["msg"] = "success";
			SendHttpRsp(evt->GetSocket(), rsp.dump());
		} catch (json::exception &e) {
			MS_LOG_WARN("json err:%s", e.what());
			json rsp;
			rsp["code"] = 1;
			rsp["msg"] = "json error";
			SendHttpRsp(evt->GetSocket(), rsp.dump());
		}
	} else {
		// get file list
		// get fileId from uri param
		string fileIdStr;
		GetParam("fileId", fileIdStr, msg.m_uri);

		char *zErrMsg = 0;
		auto pSql = MsDbMgr::Instance()->GetSql();
		std::string sql =
		    "SELECT file_id, name, size, codec, res, duration, frame_rate FROM t_file";
		if (!fileIdStr.empty()) {
			sql += " WHERE file_id = " + fileIdStr;
		}

		sqlite3_stmt *stmt;
		int rc = sqlite3_prepare_v2(pSql, sql.c_str(), -1, &stmt, NULL);
		json result = json::array();
		if (rc == SQLITE_OK) {
			while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
				json r;
				r["fileId"] = sqlite3_column_int64(stmt, 0);
				r["fileName"] = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
				r["size"] = sqlite3_column_int64(stmt, 2);
				r["codec"] = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
				r["resolution"] = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
				r["duration"] = sqlite3_column_double(stmt, 5);
				r["frameRate"] = sqlite3_column_double(stmt, 6);
				result.push_back(r);
			}
			sqlite3_finalize(stmt);
		}
		MsDbMgr::Instance()->RelSql();

		json rsp;
		rsp["code"] = 0;
		rsp["msg"] = "success";
		rsp["result"] = result;
		SendHttpRsp(evt->GetSocket(), rsp.dump());
	}
}

void MsHttpServer::FileUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	// get file id from uri param
	json rsp, r;
	string fileIdStr, netTypeStr;
	int nNetType = 0; // default use ip
	GetParam("fileId", fileIdStr, msg.m_uri);
	GetParam("netType", netTypeStr, msg.m_uri);

	if (!netTypeStr.empty()) {
		nNetType = std::stoi(netTypeStr);
	}

	if (fileIdStr.empty()) {
		rsp["code"] = 1;
		rsp["msg"] = "fileId missing";
		SendHttpRsp(evt->GetSocket(), rsp.dump());
		return;
	}

	int64_t fileId = std::stoll(fileIdStr);
	// query file name from db
	char *zErrMsg = 0;
	auto pSql = MsDbMgr::Instance()->GetSql();
	std::string sql = "SELECT name FROM t_file WHERE file_id = " + std::to_string(fileId);
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(pSql, sql.c_str(), -1, &stmt, NULL);
	std::string filename;
	if (rc == SQLITE_OK) {
		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		}
		sqlite3_finalize(stmt);
	}
	MsDbMgr::Instance()->RelSql();
	if (filename.empty()) {
		rsp["code"] = 1;
		rsp["msg"] = "file not found";
		SendHttpRsp(evt->GetSocket(), rsp.dump());
		return;
	}
	// construct file url
	auto mn = m_mediaNode.begin()->second;
	string ip = mn->nodeIp;
	if (nNetType == 1 && mn->httpMediaIP.size()) {
		ip = mn->httpMediaIP;
	}

	char bb[512];
	string rid = GenRandStr(16);
	sprintf(bb, "rtsp://%s:%d/vod/%s/%s", ip.c_str(), mn->rtspPort, rid.c_str(), filename.c_str());
	r["rtspUrl"] = bb;

	sprintf(bb, "http://%s:%d/vod/%s/ts/%s", ip.c_str(), mn->httpStreamPort, rid.c_str(),
	        filename.c_str());
	r["httpTsUrl"] = bb;

	sprintf(bb, "http://%s:%d/vod/%s/flv/%s", ip.c_str(), mn->httpStreamPort, rid.c_str(),
	        filename.c_str());
	r["httpFlvUrl"] = bb;

	rsp["code"] = 0;
	rsp["msg"] = "success";
	rsp["result"] = r;
	SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::QueryRecord(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	try {
		json ji = json::parse(body);
		string devId = ji["deviceId"].get<string>();

		auto device = MsDevMgr::Instance()->FindDevice(devId);
		if (!device) {
			MS_LOG_WARN("dev:%s not exist", devId.c_str());
			json rsp;
			rsp["code"] = 1;
			rsp["msg"] = "dev not exist";
			SendHttpRsp(evt->GetSocket(), rsp.dump());
			return;
		}

		if (device->m_protocol != GB_DEV) {
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
	} catch (json::exception &e) {
		MS_LOG_WARN("json err:%s", e.what());
		json rsp;
		rsp["code"] = 1;
		rsp["msg"] = "json error";
		SendHttpRsp(evt->GetSocket(), rsp.dump());
	}
}

void MsHttpServer::InitCatalog(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
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

void MsHttpServer::OnNodeTimer() {
	MS_LOG_DEBUG("node id:%d timer", m_nodeId);
	MsConfig *config = MsConfig::Instance();
	shared_ptr<SMediaNode> mn;
	auto it = m_mediaNode.find(m_nodeId);

	if (it == m_mediaNode.end()) {
		mn = make_shared<SMediaNode>();
		mn->node_id = m_nodeId;
		mn->m_lastUsed = 0;
		m_mediaNode.emplace(m_nodeId, mn);
	} else {
		mn = it->second;
	}

	mn->httpStreamPort = config->GetConfigInt("httpStreamPort");
	mn->rtspPort = config->GetConfigInt("rtspPort");
	mn->httpPort = config->GetConfigInt("httpPort");
	mn->httpMediaIP = config->GetConfigStr("httpMediaIP");
	mn->nodeIp = config->GetConfigStr("localBindIP");
	mn->idle = 0;
}

shared_ptr<SMediaNode> MsHttpServer::GetBestMediaNode(const string &devId, const string &bindIP) {
	if (m_mediaNode.size() == 1) {
		return m_mediaNode.begin()->second;
	}

	if (bindIP.size()) {
		for (auto it = m_mediaNode.begin(); it != m_mediaNode.end(); ++it) {
			if (it->second->nodeIp == bindIP) {
				it->second->m_lastUsed = 1;
				return it->second;
			}
		}
	}

	shared_ptr<SMediaNode> curNode;

	for (auto it = m_mediaNode.begin(); it != m_mediaNode.end(); ++it) {
		curNode = it->second;

		if (!curNode->m_lastUsed) {
			curNode->m_lastUsed = 1;
			return curNode;
		}
	}

	for (auto it = m_mediaNode.begin(); it != m_mediaNode.end(); ++it) {
		it->second->m_lastUsed = 0;
	}

	curNode = m_mediaNode.begin()->second;
	curNode->m_lastUsed = 1;
	return curNode;
}

void MsHttpServer::AddDevice(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json rsp;

	try {
		json j = json::parse(body);
		int pro = j["protocol"].get<int>();

		shared_ptr<MsGbDevice> dev = make_shared<MsGbDevice>(pro);
		JsonToDev(dev, CAMERA_TYPE, j);

		if (pro == RTSP_DEV || pro == RTMP_DEV) {
			int ret = MsDevMgr::Instance()->AddCustomDevice(dev);

			rsp["code"] = ret;
			rsp["msg"] = ret ? "add device failed" : "success";

			if (!ret) {
				json dd;
				MsDevMgr::Instance()->AssignDev(dev, dd);
				rsp["result"] = dd;
			}
		} else if (pro == ONVIF_DEV) {
			dev->m_protocol = RTSP_DEV;
			int ret = MsDevMgr::Instance()->AddCustomDevice(dev);

			rsp["code"] = ret;
			rsp["msg"] = ret ? "add device failed" : "success";

			if (ret == 0 && dev->m_user.size() && dev->m_pass.size() && dev->m_ipaddr.size()) {
				MsMsg xm;
				xm.m_msgID = MS_ONVIF_PROBE;
				xm.m_strVal = dev->m_deviceID;
				xm.m_sessinID = ++m_seqID;
				this->EnqueMsg(xm);
				m_evts.emplace(m_seqID, evt);
				return;
			} else {
				set<string> vv;
				vv.insert(dev->m_deviceID);
				MsDevMgr::Instance()->DeleteDevice(vv);

				rsp["code"] = -1;
				rsp["msg"] = "add device failed";
			}
		} else {
			rsp["code"] = -1;
			rsp["msg"] = "not support protocol";
		}
	} catch (json::exception &e) {
		MS_LOG_WARN("json err:%s", e.what());
		rsp["code"] = 1;
		rsp["msg"] = "json error";
	}

	SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::DelDevice(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json rsp;

	try {
		json j = json::parse(body);
		set<string> delDev;

		for (auto &dd : j["device"]) {
			string devid = dd.get<string>();
			shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devid);

			if (dev) {
				delDev.insert(devid);
			}
		}

		MsDevMgr::Instance()->DeleteDevice(delDev);

		rsp["code"] = 0;
		rsp["msg"] = "success";
	} catch (json::exception &e) {
		MS_LOG_WARN("json err:%s", e.what());

		rsp["code"] = 1;
		rsp["msg"] = "json error";
	}

	SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::GetGbServer(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
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

void MsHttpServer::OnGenHttpRsp(MsMsg &msg) {
	auto it = m_evts.find(msg.m_sessinID);

	if (it != m_evts.end()) {
		shared_ptr<MsEvent> evt = it->second;

		if (msg.m_intVal == 1) {
			SendHttpRspEx(evt->GetSocket(), msg.m_strVal);

			m_evts.erase(it);

			this->DelEvent(evt);
		} else {
			SendHttpRsp(evt->GetSocket(), msg.m_strVal);

			m_evts.erase(it);
		}
	}
}

void MsHttpServer::HandleMsg(MsMsg &msg) {
	switch (msg.m_msgID) {
	case MS_GEN_HTTP_RSP:
		this->OnGenHttpRsp(msg);
		break;

	case MS_MEDIA_NODE_TIMER:
		this->OnNodeTimer();
		break;

	case MS_ONVIF_PROBE:
		this->ProbeOnvif(msg.m_strVal, msg.m_sessinID);
		break;

	case MS_ONVIF_PROBE_TIMEOUT: {
		auto it = m_onvif.find(msg.m_strVal);
		if (it != m_onvif.end()) {
			MS_LOG_INFO("dev:%s probe timeout", msg.m_strVal.c_str());
			shared_ptr<MsOnvifHandler> h = it->second;

			if (h->m_evt.get())
				this->DelEvent(h->m_evt);
			h->m_evt.reset();

			set<string> vv;
			vv.insert(h->m_dev->m_deviceID);
			MsDevMgr::Instance()->DeleteDevice(vv);

			auto ite = m_evts.find(h->m_sid);
			if (ite != m_evts.end()) {
				json j;
				j["code"] = -1;
				j["msg"] = "onvif probe timeout";
				SendHttpRsp(ite->second->GetSocket(), j.dump());
				m_evts.erase(ite);
			}

			m_onvif.erase(it);
		}
	} break;

	case MS_ONVIF_PROBE_FINISH: {
		auto it = m_onvif.find(msg.m_strVal);
		if (it != m_onvif.end()) {
			MS_LOG_INFO("dev:%s probe finish", msg.m_strVal.c_str());
			shared_ptr<MsOnvifHandler> h = it->second;

			auto ite = m_evts.find(h->m_sid);
			if (ite != m_evts.end()) {
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
	} break;

	default:
		MsReactor::HandleMsg(msg);
		break;
	}
}

void MsHttpServer::HandleHttpReq(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	static map<string, httpHandle> gReqMap = {

	    {"/device", &MsHttpServer::DeviceProcess},
	    {"/device/url", &MsHttpServer::GetLiveUrl},
	    {"/device/preset", &MsHttpServer::QueryPreset},
	    {"/device/ptz", &MsHttpServer::PtzControl},

	    {"/gb/server", &MsHttpServer::GetGbServer},
	    {"/gb/catalog", &MsHttpServer::InitCatalog},
	    {"/gb/domain", &MsHttpServer::GetRegistDomain},
	    {"/gb/record", &MsHttpServer::QueryRecord},
	    {"/gb/record/url", &MsHttpServer::GetPlaybackUrl},

	    {"/file/upload", &MsHttpServer::FileUpload},
	    {"/file/url", &MsHttpServer::FileUrl},
	    {"/file", &MsHttpServer::FileProcess},

	    {"/sys/node", &MsHttpServer::GetMediaNode},
	    {"/sys/config", &MsHttpServer::SetSysConfig},
	    {"/sys/netmap", &MsHttpServer::NetMapConfig},
	};

	string uri;
	size_t p = msg.m_uri.find_first_of('?');
	if (p != string::npos) {
		uri = msg.m_uri.substr(0, p);
	} else {
		uri = msg.m_uri;
	}

	auto it = gReqMap.find(uri);

	if (it != gReqMap.end()) {
		auto func = it->second;
		(this->*func)(evt, msg, body, len);
	} else {
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

void MsHttpServer::DeviceProcess(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	if (msg.m_method == "POST") {
		this->AddDevice(evt, msg, body, len);
	} else if (msg.m_method == "DELETE") {
		this->DelDevice(evt, msg, body, len);
	} else {
		this->GetDevList(evt, msg, body, len);
	}
}

void MsHttpServer::SetSysConfig(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json jRsp;

	try {
		json j = json::parse(body);

		if (!j["useRAddr"].is_null()) {
			MsConfig::Instance()->SetConfigInt("useRAddr", j["useRAddr"].get<int>());
		}

		if (j.count("logLevel")) {
			int tt = j["logLevel"];
			MsConfig::Instance()->SetConfigInt("logLevel", tt);
			MsLog::Instance()->SetLevel(tt);
		}

		if (j.count("queryRecordType")) {
			string tt = j["queryRecordType"];
			MsConfig::Instance()->SetConfigStr("queryRecordType", tt);
		}

		if (j.count("rtpTransport")) {
			int tt = j["rtpTransport"];

			if (tt > -1 && tt < 3) {
				MsConfig::Instance()->SetConfigInt("rtpTransport", tt);
			}
		}

		if (j.count("httpMediaIP")) {
			string tt = j["httpMediaIP"];
			MsConfig::Instance()->SetConfigStr("httpMediaIP", tt);
		}

		if (j.count("gbMediaIP")) {
			string tt = j["gbMediaIP"];
			MsConfig::Instance()->SetConfigStr("gbMediaIP", tt);
		}

		if (!j["netType"].is_null()) {
			MsConfig::Instance()->SetConfigInt("netType", j["netType"].get<int>());
		}

		if (j.count("detectInterval")) {
			MsConfig::Instance()->SetConfigInt("detectInterval", j["detectInterval"].get<int>());
		}

		jRsp["code"] = 0;
		jRsp["msg"] = "success";
	} catch (json::exception &e) {
		MS_LOG_WARN("json err:%s", e.what());

		jRsp["code"] = 1;
		jRsp["msg"] = "json error";
	}

	SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::GetLiveUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json jRsp;
	int nNetType = 0;
	string deviceId, netType;
	GetParam("deviceId", deviceId, msg.m_uri);
	GetParam("netType", netType, msg.m_uri);

	if (netType.size()) {
		nNetType = atoi(netType.c_str());
	}

	if (deviceId.size() == 0) {
		try {
			json j = json::parse(body);
			deviceId = j["deviceId"].get<string>();
			nNetType = j["netType"].is_null() ? MsConfig::Instance()->GetConfigInt("netType")
			                                  : j["netType"].get<int>();
		} catch (json::exception &e) {
			MS_LOG_WARN("json err:%s", e.what());
			jRsp["code"] = 1;
			jRsp["msg"] = "json error";
			SendHttpRsp(evt->GetSocket(), jRsp.dump());
		}
	}

	shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(deviceId);

	if (!dev.get()) {
		MS_LOG_ERROR("dev:%s not exist", deviceId.c_str());
		jRsp["code"] = 1;
		jRsp["msg"] = "device not exist";
		return SendHttpRsp(evt->GetSocket(), jRsp.dump());
	}

	shared_ptr<SMediaNode> mn = this->GetBestMediaNode(deviceId, dev->m_bindIP);
	if (!mn.get()) {
		MS_LOG_ERROR("no media node");
		jRsp["code"] = 1;
		jRsp["msg"] = "no media node";
		return SendHttpRsp(evt->GetSocket(), jRsp.dump());
	}

	string ip = mn->nodeIp;
	json r;
	char bb[512];

	if (nNetType == 1 && mn->httpMediaIP.size()) {
		ip = mn->httpMediaIP;
	}

	sprintf(bb, "rtsp://%s:%d/live/%s", ip.c_str(), mn->rtspPort, deviceId.c_str());
	r["rtspUrl"] = bb;

	sprintf(bb, "http://%s:%d/live/%s.ts", ip.c_str(), mn->httpStreamPort, deviceId.c_str());
	r["httpTsUrl"] = bb;

	sprintf(bb, "http://%s:%d/live/%s.flv", ip.c_str(), mn->httpStreamPort, deviceId.c_str());
	r["httpFlvUrl"] = bb;

	jRsp["code"] = 0;
	jRsp["msg"] = "success";
	jRsp["result"] = r;
	SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::GetPlaybackUrl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json jRsp;

	try {
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

		if (type == "time") {
			nType = 3;
		} else if (type == "alarm") {
			nType = 2;
		} else if (type == "manual") {
			nType = 1;
		}

		MsConfig *conf = MsConfig::Instance();
		char bb[512];
		string rid = GenRandStr(16);

		sprintf(bb, "%s-%lld-%lld-%d", devID.c_str(), nSt, nEt, nType);
		string keyId = bb;
		string emptyIP;

		shared_ptr<SMediaNode> mn = this->GetBestMediaNode(keyId, emptyIP);
		if (!mn.get()) {
			MS_LOG_ERROR("no media node");
			jRsp["code"] = 1;
			jRsp["msg"] = "no media node";
			return SendHttpRsp(evt->GetSocket(), jRsp.dump());
		}

		string ip = mn->nodeIp;
		int netType = j["netType"].is_null() ? MsConfig::Instance()->GetConfigInt("netType")
		                                     : j["netType"].get<int>();

		if (netType == 1 && mn->httpMediaIP.size()) {
			ip = mn->httpMediaIP;
		}

		json r;
		sprintf(bb, "rtsp://%s:%d/gbvod/%s/%s", ip.c_str(), mn->rtspPort, rid.c_str(),
		        keyId.c_str());
		r["rtspUrl"] = bb;

		sprintf(bb, "http://%s:%d/gbvod/%s/%s.ts", ip.c_str(), mn->httpStreamPort, rid.c_str(),
		        keyId.c_str());
		r["httpTsUrl"] = bb;

		sprintf(bb, "http://%s:%d/gbvod/%s/%s.flv", ip.c_str(), mn->httpStreamPort, rid.c_str(),
		        keyId.c_str());
		r["httpFlvUrl"] = bb;

		jRsp["code"] = 0;
		jRsp["msg"] = "success";
		jRsp["result"] = r;
	} catch (json::exception &e) {
		MS_LOG_WARN("json err:%s", e.what());

		jRsp["code"] = 1;
		jRsp["msg"] = "json error";
	}

	SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::GetDevList(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json rsp;
	string deviceId;
	GetParam("deviceId", deviceId, msg.m_uri);

	if (deviceId.size()) {
		MsDevMgr::Instance()->GetOneDev(rsp, deviceId);
	} else {
		MsDevMgr::Instance()->GetAllDevice(rsp, 0, 0);
	}

	SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::GetRegistDomain(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	// send MsMsg to gb server to get regist domain
	MsMsg qr;
	qr.m_sessinID = ++m_seqID;
	qr.m_msgID = MS_GET_REGIST_DOMAIN;
	qr.m_dstType = MS_GB_SERVER;
	qr.m_dstID = 1;
	this->PostMsg(qr);
	m_evts[qr.m_sessinID] = evt;
}

void MsHttpServer::GetMediaNode(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json j;

	for (auto &nn : m_mediaNode) {
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

void MsHttpServer::PtzControl(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json rsp;

	rsp["code"] = 0;
	rsp["msg"] = "ok";

	try {
		json j = json::parse(body);
		string deviceId = j["deviceId"].get<string>();
		string presetID = j["presetID"].is_null() ? "" : j["presetID"].get<string>();
		int ptzCmd = j["ptzCmd"].get<int>();
		int timeout = 500;

		if (!j["timeout"].is_null()) {
			timeout = j["timeout"].get<int>();
			if (timeout < 1 || timeout > 10000)
				timeout = 500;
		}

		shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(deviceId);

		if (!dev.get()) {
			rsp["code"] = 1;
			rsp["msg"] = "dev not exist";
			return SendHttpRsp(evt->GetSocket(), rsp.dump());
		}

		if (ptzCmd < 1 || ptzCmd > 14 || ptzCmd == 10) {
			rsp["code"] = 1;
			rsp["msg"] = "param error";
			return SendHttpRsp(evt->GetSocket(), rsp.dump());
		}

		if (dev->m_protocol == GB_DEV) {
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

		if ((dev->m_protocol == RTSP_DEV || dev->m_protocol == ONVIF_DEV) &&
		    dev->m_onvifptzurl.size() && dev->m_onvifprofile.size()) {
			thread ptzx(MsOnvifHandler::OnvifPtzControl, dev->m_user, dev->m_pass,
			            dev->m_onvifptzurl, dev->m_onvifprofile, presetID, ptzCmd, timeout);
			ptzx.detach();
		} else {
			rsp["code"] = 1;
			rsp["msg"] = "dev not support ptz";
			return SendHttpRsp(evt->GetSocket(), rsp.dump());
		}
	} catch (json::exception &e) {
		MS_LOG_WARN("json err:%s", e.what());
		rsp["code"] = 1;
		rsp["msg"] = "json error";
	}

	SendHttpRsp(evt->GetSocket(), rsp.dump());
}

void MsHttpServer::NetMapConfig(shared_ptr<MsEvent> evt, MsHttpMsg &msg, char *body, int len) {
	json jRsp;
	jRsp["code"] = 0;
	jRsp["msg"] = "success";

	try {
		json j = json::parse(body);
		string cmd = j["cmd"].is_null() ? "get" : j["cmd"].get<string>();

		if (cmd == "get") {
			json &obj = MsConfig::Instance()->GetConfigObj();
			jRsp["result"] = obj["netMap"];
		} else if (cmd == "add") {
			string fromIP = j["fromIP"].get<string>();
			string toIP = j["toIP"].get<string>();
			MsDevMgr::Instance()->AddMapIP(fromIP, toIP);
		} else if (cmd == "del") {
			string fromIP = j["fromIP"].get<string>();
			MsDevMgr::Instance()->DelMapIP(fromIP);
		}
	} catch (json::exception &e) {
		json &obj = MsConfig::Instance()->GetConfigObj();
		jRsp["result"] = obj["netMap"];
	}

	SendHttpRsp(evt->GetSocket(), jRsp.dump());
}

void MsHttpServer::ProbeOnvif(string &devid, int sid) {
	if (m_onvif.count(devid)) {
		MS_LOG_ERROR("dev:%s probing", devid.c_str());
		return;
	}

	shared_ptr<MsGbDevice> dev = MsDevMgr::Instance()->FindDevice(devid);
	shared_ptr<MsSocket> udp_sock = make_shared<MsSocket>(AF_INET, SOCK_DGRAM, 0);
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

	shared_ptr<MsOnvifHandler> handler = make_shared<MsOnvifHandler>(shared_from_this(), dev, sid);

	shared_ptr<MsEvent> evt =
	    make_shared<MsEvent>(udp_sock, MS_FD_READ, dynamic_pointer_cast<MsEventHandler>(handler));

	this->AddEvent(evt);

	handler->m_evt = evt;
	m_onvif[devid] = handler;

	MsMsg tt;
	tt.m_msgID = MS_ONVIF_PROBE_TIMEOUT;
	tt.m_strVal = devid;
	this->AddTimer(tt, 10);
}
