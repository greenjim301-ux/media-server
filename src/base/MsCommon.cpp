#include "MsCommon.h"
#include "MsLog.h"
#include "MsOsConfig.h"
#include <random>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>

bool IsHeaderComplete(char *p) { return NULL != strstr(p, "\r\n\r\n"); }

void SkipToSpace(char *&p) {
	while (*p != ' ' && *p) {
		++p;
	}
}

void SkipSpace(char *&p) {
	while (*p == ' ' && *p != '\r' && *p != '\n' && *p != '\0') {
		++p;
	}
}

void ParseReqLine(char *&buf, string &method, string &uri, string &version) {
	char *p = buf;
	char *p2 = buf;

	SkipToSpace(p2);
	method.assign(p, p2 - p);

	SkipSpace(p2);

	p = p2;
	SkipToSpace(p2);
	uri.assign(p, p2 - p);

	SkipSpace(p2);

	p = p2;
	SkipToLineEnd(p2);
	version.assign(p, p2 - p);

	SkipLineEnd(p2);
	buf = p2;
}

int GetHeaderLine(char *&buf, string &line) {
	char *p2 = buf;
	SkipToLineEnd(p2);

	line.assign(buf, p2 - buf);

	SkipLineEnd(p2);
	buf = p2;

	return line.size();
}

void SkipToLineEnd(char *&p) {
	while (*p != '\r' && *p != '\n' && *p != '\0') {
		++p;
	}
}

void SkipLineEnd(char *&p) {
	if (*p == '\r') {
		++p;
	}

	if (*p == '\n') {
		++p;
	}
}

void AddHeaderLine(string &rsp, const string &key, const string &value) {
	rsp += key;
	rsp += ": ";
	rsp += value;

	rsp += "\r\n";
}

void avio_w8(uint8_t *&s, int b) { *s++ = b; }

void avio_wb16(uint8_t *&s, unsigned int val) {
	avio_w8(s, (int)val >> 8);
	avio_w8(s, (uint8_t)val);
}

static const uint8_t *ff_avc_find_startcode_internal(const uint8_t *p, const uint8_t *end) {
	const uint8_t *a = p + 4 - ((intptr_t)p & 3);

	for (end -= 3; p < a && p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	for (end -= 3; p < end; p += 4) {
		uint32_t x = *(const uint32_t *)p;
		//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
		//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
		if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
			if (p[1] == 0) {
				if (p[0] == 0 && p[2] == 1)
					return p;
				if (p[2] == 0 && p[3] == 1)
					return p + 1;
			}
			if (p[3] == 0) {
				if (p[2] == 0 && p[4] == 1)
					return p + 2;
				if (p[4] == 0 && p[5] == 1)
					return p + 3;
			}
		}
	}

	for (end += 3; p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	return end + 3;
}

MsComHeader::MsComHeader(const char *key) : m_key(key), m_exist(false) {}

void MsComHeader::Dump(string &rsp) {
	if (m_exist) {
		AddHeaderLine(rsp, m_key, m_value);
	}
}

int64_t MsComIntVal::GetIntVal() {
	if (m_exist) {
		return stoll(m_value);
	} else {
		return 0;
	}
}

void MsComIntVal::SetIntVal(int64_t len) {
	m_exist = true;
	m_value = to_string(len);
}

string MsComAuth::GetAttr(const char *key) {
	const char *s = m_value.c_str();
	const char *p1 = strstr(s, key);
	if (!p1) {
		return string();
	}

	p1 += strlen(key) + 2; //="
	const char *p2 = strchr(p1, '"');
	if (!p2) {
		return string();
	}

	return string(p1, p2 - p1);
}

MsComMsg::MsComMsg()
    : m_contentLength("Content-Length"), m_contentType("Content-Type"), m_body(NULL), m_bodyLen(0) {
}

void MsComMsg::SetBody(const char *body, int len) {
	m_body = body;
	m_bodyLen = len;
	m_contentLength.SetIntVal(len);
}

void BuildFirstLine(string &rsp, string &s1, string &s2, string &s3) {
	rsp += s1;
	rsp += ' ';
	rsp += s2;
	rsp += ' ';
	rsp += s3;
	rsp += "\r\n";
}

int64_t GetCurMs() {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	int64_t sec = ts.tv_sec;
	int64_t msec = ts.tv_nsec / 1000000;

	return (int64_t)sec * 1000 + msec;
}

void GbkToUtf8(string &strSrc, const char *src_str) {
	if (src_str == nullptr) {
		return;
	}

	iconv_t cd;
	size_t sn = strlen(src_str);
	if (sn == 0) {
		return;
	}

	char *ss = (char *)malloc(sn + 1);
	strcpy(ss, src_str);

	size_t dn = 30 * sn + 1;
	char *dd = (char *)malloc(dn);

	char *ss1 = ss;
	char *dd1 = dd;

	char **pin = &ss1;
	char **pout = &dd1;

	cd = iconv_open("utf8", "gbk");
	if (cd == 0)
		return;

	if (iconv(cd, pin, &sn, pout, &dn) == -1)
		return;
	iconv_close(cd);
	**pout = '\0';

	strSrc = dd;

	free(ss);
	free(dd);
}

time_t StrTimeToUnixTime(string &timeStamp) {
	int month, day, year, hour, min, sec;

	sscanf(timeStamp.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec);

	--month;

	/*
	 * shift new year to March 1 and start months from 1 (not 0),
	 * it is needed for Gauss' formula
	 */

	if (--month <= 0) {
		month += 12;
		year -= 1;
	}

	/* Gauss' formula for Gregorian days since March 1, 1 BC */

	uint64_t time = (uint64_t)(
	                    /* days in years including leap years since March 1, 1 BC */

	                    365 * year + year / 4 - year / 100 +
	                    year / 400

	                    /* days before the month */

	                    + 367 * month / 12 -
	                    30

	                    /* days before the day */

	                    + day -
	                    1

	                    /*
	                     * 719527 days were between March 1, 1 BC and March 1, 1970,
	                     * 31 and 28 days were in January and February 1970
	                     */

	                    - 719527 + 31 + 28) *
	                    86400 +
	                hour * 3600 + min * 60 + sec;

	return (time_t)time;
}

string GmtTimeToStr(int64_t t) {
	int yday;
	uint32_t sec, min, hour, mday, mon, year, wday, days, leap;

	/* the calculation is valid for positive time_t only */

	if (t < 0) {
		t = 0;
	}

	days = t / 86400;
	sec = t % 86400;

	/*
	 * no more than 4 year digits supported,
	 * truncate to December 31, 9999, 23:59:59
	 */

	if (days > 2932896) {
		days = 2932896;
		sec = 86399;
	}

	/* January 1, 1970 was Thursday */

	wday = (4 + days) % 7;

	hour = sec / 3600;
	sec %= 3600;
	min = sec / 60;
	sec %= 60;

	/*
	 * the algorithm based on Gauss' formula,
	 * see src/core/ngx_parse_time.c
	 */

	/* days since March 1, 1 BC */
	days = days - (31 + 28) + 719527;

	/*
	 * The "days" should be adjusted to 1 only, however, some March 1st's go
	 * to previous year, so we adjust them to 2.  This causes also shift of the
	 * last February days to next year, but we catch the case when "yday"
	 * becomes negative.
	 */

	year = (days + 2) * 400 / (365 * 400 + 100 - 4 + 1);

	yday = days - (365 * year + year / 4 - year / 100 + year / 400);

	if (yday < 0) {
		leap = (year % 4 == 0) && (year % 100 || (year % 400 == 0));
		yday = 365 + leap + yday;
		year--;
	}

	/*
	 * The empirical formula that maps "yday" to month.
	 * There are at least 10 variants, some of them are:
	 *     mon = (yday + 31) * 15 / 459
	 *     mon = (yday + 31) * 17 / 520
	 *     mon = (yday + 31) * 20 / 612
	 */

	mon = (yday + 31) * 10 / 306;

	/* the Gauss' formula that evaluates days before the month */

	mday = yday - (367 * mon / 12 - 30) + 1;

	if (yday >= 306) {

		year++;
		mon -= 10;

		/*
		 * there is no "yday" in Win32 SYSTEMTIME
		 *
		 * yday -= 306;
		 */
	} else {

		mon += 2;

		/*
		 * there is no "yday" in Win32 SYSTEMTIME
		 *
		 * yday += 31 + 28 + leap;
		 */
	}

	char bb[32];
	sprintf(bb, "%d-%02d-%02dT%02d:%02d:%02d", year, mon, mday, hour, min, sec);

	return string(bb);
}

void CvtToHex(unsigned char *in, unsigned char *out) {
	unsigned short i;
	unsigned char j;
	for (i = 0; i < 16; i++) {
		j = (in[i] >> 4) & 0xf;
		if (j <= 9)
			out[i * 2] = (j + '0');
		else
			out[i * 2] = (j + 'a' - 10);
		j = in[i] & 0xf;
		if (j <= 9)
			out[i * 2 + 1] = (j + '0');
		else
			out[i * 2 + 1] = (j + 'a' - 10);
	}
}

void SleepMs(int ms) { usleep(ms * 1000); }

std::string EncodeBase64(const unsigned char *data, size_t in_len) {
	static constexpr char sEncodingTable[] = {
	    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

	size_t out_len = 4 * ((in_len + 2) / 3);
	std::string ret(out_len, '\0');
	size_t i;
	char *p = const_cast<char *>(ret.c_str());

	for (i = 0; i < in_len - 2; i += 3) {
		*p++ = sEncodingTable[(data[i] >> 2) & 0x3F];
		*p++ = sEncodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
		*p++ = sEncodingTable[((data[i + 1] & 0xF) << 2) | ((int)(data[i + 2] & 0xC0) >> 6)];
		*p++ = sEncodingTable[data[i + 2] & 0x3F];
	}
	if (i < in_len) {
		*p++ = sEncodingTable[(data[i] >> 2) & 0x3F];
		if (i == (in_len - 1)) {
			*p++ = sEncodingTable[((data[i] & 0x3) << 4)];
			*p++ = '=';
		} else {
			*p++ = sEncodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
			*p++ = sEncodingTable[((data[i + 1] & 0xF) << 2)];
		}
		*p++ = '=';
	}

	return ret;
}

void DecodeBase64(const std::string &input, std::vector<unsigned char> &out) {
	static constexpr unsigned char kDecodingTable[] = {
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62,
	    64, 64, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,
	    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	    23, 24, 25, 64, 64, 64, 64, 64, 64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
	    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

	size_t in_len = input.size();
	if (in_len % 4 != 0)
		return;

	size_t out_len = in_len / 4 * 3;
	if (input[in_len - 1] == '=')
		out_len--;
	if (input[in_len - 2] == '=')
		out_len--;

	out.resize(out_len);

	for (size_t i = 0, j = 0; i < in_len;) {
		uint32_t a = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
		uint32_t b = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
		uint32_t c = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
		uint32_t d = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];

		uint32_t triple = (a << 3 * 6) + (b << 2 * 6) + (c << 1 * 6) + (d << 0 * 6);

		if (j < out_len)
			out[j++] = (triple >> 2 * 8) & 0xFF;
		if (j < out_len)
			out[j++] = (triple >> 1 * 8) & 0xFF;
		if (j < out_len)
			out[j++] = (triple >> 0 * 8) & 0xFF;
	}
}

string GenRandStr(int len) {
	char tmp;
	string buffer;

	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_int_distribution<int> dist(0, 10000);

	for (int i = 0; i < len; i++) {
		tmp = dist(gen) % 36;
		if (tmp < 10) {
			tmp += '0';
		} else {
			tmp -= 10;
			tmp += 'A';
		}
		buffer += tmp;
	}
	return buffer;
}

std::vector<std::string> SplitString(const std::string &input, const std::string &delimiter) {
	std::vector<std::string> result;

	if (input.empty() || delimiter.empty()) {
		result.push_back(input);
		return result;
	}

	size_t start = 0;
	size_t end = input.find(delimiter);

	while (end != std::string::npos) {
		result.push_back(input.substr(start, end - start));
		start = end + delimiter.length();
		end = input.find(delimiter, start);
	}

	// Add the last substring
	result.push_back(input.substr(start));

	return result;
}

int SendSmallBlock(const uint8_t *buf, int buf_size, int fd) {
	int ret = 0;
	const uint8_t *pBuf = buf;
	int pLen = buf_size;

	while (pLen > 0) {
		ret = send(fd, pBuf, pLen, 0);
		if (ret >= 0) {
			pLen -= ret;
			pBuf += ret;
		} else if (MS_LAST_ERROR == EAGAIN) {
			continue;
		} else if (MS_LAST_ERROR == EINTR) {
			continue;
		} else {
			MS_LOG_ERROR("send error,err:%d", MS_LAST_ERROR);
			return -1;
		}
	}

	return 0;
}