// Copyright 2025-2026 Yolo Technologies, Inc. All Rights Reserved. https://www.kbelab.com/

#ifndef KBE_BOTS_ACTIVE_REPORT_HANDLER_H
#define KBE_BOTS_ACTIVE_REPORT_HANDLER_H

#include "common/common.h"
#include "common/tasks.h"
#include "common/timer.h"
#include "helper/debug_helper.h"

namespace KBEngine {

	//class ServerApp;
	class ClientApp;

	class BotsActiveReportHandler : public TimerHandler
	{
	public:
		enum TimeOutType
		{
			TIMEOUT_ACTIVE_TICK,
			TIMEOUT_MAX
		};

		BotsActiveReportHandler(ClientApp* pApp);
		virtual ~BotsActiveReportHandler();

		void startActiveTick(float period);

		void cancel();

	protected:
		virtual void handleTimeout(TimerHandle handle, void* arg);

		ClientApp* pApp_;
		TimerHandle pActiveTimerHandle_;

	};

}

#endif // KBE_BOTS_ACTIVE_REPORT_HANDLER_H
