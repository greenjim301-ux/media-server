#pragma once
#include <string>
#include <map>
#include <memory>
#include <stdint.h>
#include <vector>

using namespace std;

#define DEF_BUF_SIZE 64 * 1024

#define AV_RB16(x) ((((const uint8_t *)(x))[0] << 8) | ((const uint8_t *)(x))[1])
#define AV_RB32(x) ((((const uint8_t *)(x))[0] << 24) | \
					(((const uint8_t *)(x))[1] << 16) | \
					(((const uint8_t *)(x))[2] << 8) |  \
					((const uint8_t *)(x))[3])
#define RTP_FLAG_MARKER 0x2 ///< RTP marker bit was set for this packet

bool IsHeaderComplete(char *p);
void SkipToSpace(char *&p);
void SkipSpace(char *&p);
void ParseReqLine(char *&buf, string &method, string &uri, string &version);
int GetHeaderLine(char *&buf, string &line);
void SkipToLineEnd(char *&p);
void SkipLineEnd(char *&p);
void AddHeaderLine(string &rsp, const string &key, const string &value);
void BuildFirstLine(string &rsp, string &s1, string &s2, string &s3);
string GmtTimeToStr(int64_t t);

void avio_w8(uint8_t *&s, int b);
void avio_wb16(uint8_t *&s, unsigned int val);

int64_t GetCurMs();
void GbkToUtf8(string &src_str, const char *src);
time_t StrTimeToUnixTime(string &timeStamp);

class MsComHeader
{
public:
	MsComHeader(const char *key);

	void Dump(string &rsp);

	inline void SetValue(const string &value)
	{
		m_value = value;
		m_exist = true;
	}

	string m_key;
	string m_value;
	bool m_exist;
};

class MsComIntVal : public MsComHeader
{
public:
	MsComIntVal(const char *key) : MsComHeader(key) {}

	int64_t GetIntVal();
	void SetIntVal(int64_t len);
};

class MsComAuth : public MsComHeader
{
public:
	MsComAuth(const char *key) : MsComHeader(key) {}

	string GetAttr(const char *key);
};

class MsComMsg
{
public:
	MsComMsg();

	void SetBody(const char *body, int len);

	string m_method;
	string m_uri;
	string m_version;

	string m_status;
	string m_reason;

	MsComIntVal m_contentLength;
	MsComHeader m_contentType;

	const char *m_body;
	int m_bodyLen;
};


struct SGbContext
{
	string gbID;
	string gbCallID;
	string rtpIP;
	int rtpPort;
	int transport;
	int type;
	string startTime;
	string endTime;
};


struct SData
{
	uint8_t *m_buf;
	int m_len;
};

struct SMediaNode
{
    int node_id;
    int idle;
    int m_lastUsed;
    int httpStreamPort;
    int rtspPort;
    int httpPort;
    string httpMediaIP;
    string nodeIp;
};

class SPtzCmd
{
public:
	string m_devid;
	string m_presetID;
	int m_ptzCmd;
	int m_timeout;
};

enum TRASNSPORT
{
	EN_UDP = 0,
	EN_TCP_ACTIVE,
	EN_TCP_PASSIVE
};

std::string EncodeBase64(const unsigned char *data, size_t in_len);
void DecodeBase64(const std::string &input, std::vector<unsigned char> &out);
string GenRandStr(int len);
void CvtToHex(unsigned char *in, unsigned char *out);
void SleepMs(int ms);
vector<string> SplitString(const string &input, const string &delimiter);
int SendSmallBlock(const uint8_t *buf, int buf_size, int fd);