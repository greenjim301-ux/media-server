#include "MsApp.h"
#include "MsConfig.h"
#include "MsDbMgr.h"
#include "MsDevMgr.h"
#include "MsGbServer.h"
#include "MsHttpServer.h"
#include "MsHttpStream.h"
#include "MsLog.h"
#include "MsOsConfig.h"
#include "MsRtspSink.h"
#include "MsTimer.h"
#include <iostream>

int main(int argc, char *argv[]) {
	signal(SIGPIPE, SIG_IGN);

	MsConfig *config = MsConfig::Instance();
	config->LoadConfig();

	MsLog::Instance()->SetLevel(config->GetConfigInt("logLevel"));
	MsLog::Instance()->Run();

	MsTimer::Instance()->Run();

	if (MsDbMgr::Instance()->Init()) {
		printf("init db mgr failed\n");
		return -1;
	}

	MsDevMgr::Instance()->LoadDevice();

	shared_ptr<MsHttpServer> httpServer = make_shared<MsHttpServer>(MS_HTTP_SERVER, 1);
	httpServer->Run();

	shared_ptr<MsGbServer> gbServer = make_shared<MsGbServer>(MS_GB_SERVER, 1);
	gbServer->Run();

	shared_ptr<MsRtspServer> rtsp = make_shared<MsRtspServer>(MS_RTSP_SERVER, 1);
	rtsp->Run();

	shared_ptr<MsHttpStream> hs = make_shared<MsHttpStream>(MS_HTTP_STREAM, 1);
	hs->Run();

	printf("media server v1.0.0 running\n");

	MsApp::Instance()->Run();

	return 0;
}
