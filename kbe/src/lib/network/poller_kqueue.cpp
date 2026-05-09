// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com


#include "poller_kqueue.h"
#include "helper/profile.h"
#ifdef HAS_KQUEUE
#include <sys/event.h>
#include <time.h>
#endif

namespace KBEngine {

#ifdef HAS_KQUEUE
namespace
{
ProfileVal g_kqueueIdleProfile("Idle");
}

namespace Network
{

//-------------------------------------------------------------------------------------
KqueuePoller::KqueuePoller() :
	kqfd_(kqueue())
{
	if (kqfd_ == -1)
	{
		ERROR_MSG(fmt::format("KqueuePoller::KqueuePoller: kqueue create failed: {}\n",
				kbe_strerror()));
	}
}

//-------------------------------------------------------------------------------------
KqueuePoller::~KqueuePoller()
{
	if (kqfd_ != -1)
	{
		close(kqfd_);
	}
}

//-------------------------------------------------------------------------------------
bool KqueuePoller::doRegister(int fd, bool isRead, bool isRegister)
{
	struct kevent change;
	EV_SET(&change,
		static_cast<uintptr_t>(fd),
		isRead ? EVFILT_READ : EVFILT_WRITE,
		isRegister ? (EV_ADD | EV_ENABLE) : EV_DELETE,
		0,
		0,
		NULL);

	if (kevent(kqfd_, &change, 1, NULL, 0, NULL) < 0)
	{
		// EV_DELETE on an unregistered fd is not fatal for caller semantics.
		if (!isRegister && errno == ENOENT)
		{
			return true;
		}

		const char* MESSAGE = "KqueuePoller::doRegister: Failed to {} {} file "
				"descriptor {} ({})\n";
		if (errno == EBADF)
		{
			WARNING_MSG(fmt::format(MESSAGE,
					(isRegister ? "add" : "remove"),
					(isRead ? "read" : "write"),
					fd,
					kbe_strerror()));
		}
		else
		{
			ERROR_MSG(fmt::format(MESSAGE,
					(isRegister ? "add" : "remove"),
					(isRead ? "read" : "write"),
					fd,
					kbe_strerror()));
		}

		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------
int KqueuePoller::processPendingEvents(double maxWait)
{
	const int MAX_EVENTS = 10;
	struct kevent events[MAX_EVENTS];
	struct timespec timeout;
	timeout.tv_sec = static_cast<time_t>(maxWait);
	timeout.tv_nsec = static_cast<long>((maxWait - static_cast<double>(timeout.tv_sec)) * 1000000000.0);

#if ENABLE_WATCHERS
	g_kqueueIdleProfile.start();
#else
	uint64 startTime = timestamp();
#endif

	KBEConcurrency::onStartMainThreadIdling();
	int nfds = kevent(kqfd_, NULL, 0, events, MAX_EVENTS, &timeout);
	KBEConcurrency::onEndMainThreadIdling();


#if ENABLE_WATCHERS
	g_kqueueIdleProfile.stop();
	spareTime_ += g_kqueueIdleProfile.lastTime_;
#else
	spareTime_ += timestamp() - startTime;
#endif

	if (nfds < 0)
	{
		WARNING_MSG(fmt::format("KqueuePoller::processPendingEvents: "
			"error in kevent(): {}\n", kbe_strerror()));
		return nfds;
	}

	for (int i = 0; i < nfds; ++i)
	{
		int fd = static_cast<int>(events[i].ident);
		if ((events[i].flags & EV_ERROR) || (events[i].flags & EV_EOF))
		{
			this->triggerError(fd);
		}
		else if (events[i].filter == EVFILT_READ)
		{
			this->triggerRead(fd);
		}
		else if (events[i].filter == EVFILT_WRITE)
		{
			this->triggerWrite(fd);
		}
	}

	return nfds;
}

}

#endif // HAS_KQUEUE

}
