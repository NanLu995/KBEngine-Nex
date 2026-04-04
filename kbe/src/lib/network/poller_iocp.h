// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBE_IOCP_POLLER_H
#define KBE_IOCP_POLLER_H

#include "event_poller.h"

#if KBE_PLATFORM == PLATFORM_WIN32

namespace KBEngine {
namespace Network
{

class IocpPoller : public EventPoller
{
public:
	IocpPoller();
	~IocpPoller() override;

	IocpPoller* asIocpPoller() override { return this; }

	int processPendingEvents(double maxWait) override;

	bool takeAcceptedSocket(int fd, KBESOCKET& acceptedSocket);

protected:
	bool doRegisterForRead(int fd) override;
	bool doRegisterForWrite(int fd) override;

	bool doDeregisterForRead(int fd) override;
	bool doDeregisterForWrite(int fd) override;

private:
	enum SocketKind
	{
		SOCKET_KIND_UNKNOWN = 0,
		SOCKET_KIND_TCP,
		SOCKET_KIND_UDP,
		SOCKET_KIND_LISTENER
	};

	struct SocketState
	{
		explicit SocketState(KBESOCKET socketArg);

		OVERLAPPED overlapped;
		KBESOCKET socket;
		SocketKind kind;
		bool associated;
		bool registeredRead;
		bool pendingRead;
		KBESOCKET acceptSocket;
		LPFN_ACCEPTEX acceptExFn;
		char probeByte;
		char acceptBuffer[(sizeof(sockaddr_in) + 16) * 2];
		sockaddr_in udpAddr;
		int udpAddrLen;
	};

	typedef std::unique_ptr<SocketState> SocketStatePtr;
	typedef std::map<int, SocketStatePtr> SocketStates;
	typedef std::deque<KBESOCKET> AcceptedSockets;
	typedef std::map<int, AcceptedSockets> AcceptedSocketMap;

	bool ensureAssociated(SocketState& state, int fd);
	bool ensureReadArmed(int fd, SocketState& state);
	bool armTcpRead(SocketState& state);
	bool armUdpRead(SocketState& state);
	bool armAccept(SocketState& state);
	bool tryDetermineSocketKind(KBESOCKET socket, SocketKind& kind) const;
	bool loadAcceptEx(SocketState& state);
	void handleCompletion(ULONG_PTR completionKey, LPOVERLAPPED overlapped, bool success, DWORD errorCode);
	void cleanupStateIfUnused(int fd);
	void closeAcceptedSockets(AcceptedSockets& acceptedSockets);

	HANDLE completionPort_;
	SocketStates socketStates_;
	AcceptedSocketMap acceptedSockets_;
};

}
}

#endif // KBE_PLATFORM == PLATFORM_WIN32

#endif // KBE_IOCP_POLLER_H
