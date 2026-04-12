// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#include "client_lib/kbemain.h"
#include "bots.h"

#undef DEFINE_IN_INTERFACE
#include "client_lib/client_interface.h"
#define DEFINE_IN_INTERFACE
#define CLIENT
#include "client_lib/client_interface.h"

#undef DEFINE_IN_INTERFACE
#include "baseapp/baseapp_interface.h"
#define DEFINE_IN_INTERFACE
#include "baseapp/baseapp_interface.h"

#undef DEFINE_IN_INTERFACE
#include "loginapp/loginapp_interface.h"
#define DEFINE_IN_INTERFACE
#include "loginapp/loginapp_interface.h"

#undef DEFINE_IN_INTERFACE
#include "cellapp/cellapp_interface.h"
#define DEFINE_IN_INTERFACE
#include "cellapp/cellapp_interface.h"

#undef DEFINE_IN_INTERFACE
#include "baseappmgr/baseappmgr_interface.h"
#define DEFINE_IN_INTERFACE
#include "baseappmgr/baseappmgr_interface.h"

#undef DEFINE_IN_INTERFACE
#include "dbmgr/dbmgr_interface.h"
#define DEFINE_IN_INTERFACE
#include "dbmgr/dbmgr_interface.h"

#undef DEFINE_IN_INTERFACE
#include "machine/machine_interface.h"
#define DEFINE_IN_INTERFACE
#include "machine/machine_interface.h"

#undef DEFINE_IN_INTERFACE
#include "cellappmgr/cellappmgr_interface.h"
#define DEFINE_IN_INTERFACE
#include "cellappmgr/cellappmgr_interface.h"

#undef DEFINE_IN_INTERFACE
#include "tools/logger/logger_interface.h"
#define DEFINE_IN_INTERFACE
#include "tools/logger/logger_interface.h"

#undef DEFINE_IN_INTERFACE
#include "tools/interfaces/interfaces_interface.h"
#define DEFINE_IN_INTERFACE
#include "tools/interfaces/interfaces_interface.h"

using namespace KBEngine;

namespace
{
void installBotsCrashTestEnv(int argc, char* argv[])
{
	for (int i = 1; i < argc; ++i)
	{
		std::string cmd = argv[i];
		const std::string prefix = "--crash-test=";
		const std::string::size_type pos = cmd.find(prefix);
		if (pos == std::string::npos)
			continue;

		std::string crashType = cmd.substr(pos + prefix.size());
		if (!crashType.empty())
			setenv("KBE_BOTS_CRASH_TEST", crashType.c_str(), 1);

		return;
	}
}
}

int KBENGINE_MAIN(int argc, char* argv[])
{
	g_componentType = BOTS_TYPE;
	installBotsCrashTestEnv(argc, argv);
	return kbeMainT<Bots>(argc, argv, g_componentType, -1, -1, -1, -1, "", 0, 0, "");
}

