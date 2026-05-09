// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBE_KQUEUE_POLLER_H
#define KBE_KQUEUE_POLLER_H

#include "event_poller.h"

#if KBE_PLATFORM == PLATFORM_APPLE
#define HAS_KQUEUE
#endif

namespace KBEngine {
namespace Network
{

#ifdef HAS_KQUEUE
class KqueuePoller : public EventPoller
{
public:
	KqueuePoller();
	virtual ~KqueuePoller();

	int getFileDescriptor() const { return kqfd_; }

protected:
	virtual bool doRegisterForRead(int fd)
		{ return this->doRegister(fd, true, true); }

	virtual bool doRegisterForWrite(int fd)
		{ return this->doRegister(fd, false, true); }

	virtual bool doDeregisterForRead(int fd)
		{ return this->doRegister(fd, true, false); }

	virtual bool doDeregisterForWrite(int fd)
		{ return this->doRegister(fd, false, false); }

	virtual int processPendingEvents(double maxWait);

	bool doRegister(int fd, bool isRead, bool isRegister);

private:
	int kqfd_;
};
#endif // HAS_KQUEUE

}
}
#endif // KBE_KQUEUE_POLLER_H
