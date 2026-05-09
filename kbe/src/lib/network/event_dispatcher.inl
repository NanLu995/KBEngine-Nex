// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

namespace KBEngine { 
namespace Network
{
#if KBE_PLATFORM == PLATFORM_WIN32
INLINE bool EventDispatcher::registerReadFileDescriptor(KBESOCKET fd, InputNotificationHandler * handler)
{
	KBE_ASSERT(fd <= static_cast<KBESOCKET>(std::numeric_limits<int>::max()));
	return registerReadFileDescriptor(static_cast<int>(fd), handler);
}

INLINE bool EventDispatcher::deregisterReadFileDescriptor(KBESOCKET fd)
{
	KBE_ASSERT(fd <= static_cast<KBESOCKET>(std::numeric_limits<int>::max()));
	return deregisterReadFileDescriptor(static_cast<int>(fd));
}

INLINE bool EventDispatcher::registerWriteFileDescriptor(KBESOCKET fd, OutputNotificationHandler * handler)
{
	KBE_ASSERT(fd <= static_cast<KBESOCKET>(std::numeric_limits<int>::max()));
	return registerWriteFileDescriptor(static_cast<int>(fd), handler);
}

INLINE bool EventDispatcher::deregisterWriteFileDescriptor(KBESOCKET fd)
{
	KBE_ASSERT(fd <= static_cast<KBESOCKET>(std::numeric_limits<int>::max()));
	return deregisterWriteFileDescriptor(static_cast<int>(fd));
}
#endif

INLINE TimerHandle EventDispatcher::addTimer(int64 microseconds,
	TimerHandler * handler, void * arg)
{
	return this->addTimerCommon(microseconds, handler, arg, true);
}

INLINE void EventDispatcher::breakProcessing(bool breakState)
{
	if(breakState)
		breakProcessing_ = EVENT_DISPATCHER_STATUS_BREAK_PROCESSING;
	else
		breakProcessing_ = EVENT_DISPATCHER_STATUS_RUNNING;
}

INLINE void EventDispatcher::setWaitBreakProcessing()
{
	breakProcessing_ = EVENT_DISPATCHER_STATUS_WAITING_BREAK_PROCESSING;
}

INLINE bool EventDispatcher::hasBreakProcessing() const 
{ 
	return breakProcessing_ == EVENT_DISPATCHER_STATUS_BREAK_PROCESSING; 
}

INLINE bool EventDispatcher::waitingBreakProcessing() const 
{ 
	return breakProcessing_ == EVENT_DISPATCHER_STATUS_WAITING_BREAK_PROCESSING; 
}

INLINE double EventDispatcher::maxWait() const
{
	return maxWait_;
}

INLINE void EventDispatcher::maxWait(double seconds)
{
	maxWait_ = seconds;
}

} // namespace Network
}
// event_dispatcher.inl
