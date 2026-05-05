// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBE_IOCP_POLLER_H
#define KBE_IOCP_POLLER_H

#include "event_poller.h"

#if KBE_PLATFORM == PLATFORM_WIN32

#include "network/address.h"

namespace KBEngine {
namespace Network
{

class IocpPoller : public EventPoller
{
public:
	IocpPoller();
	~IocpPoller() override;

	int processPendingEvents(double maxWait) override;

	bool takeAcceptedSocket(int fd, KBESOCKET& acceptedSocket);
	bool takeTcpReceivedData(int fd, std::vector<char>& data, bool& disconnected, DWORD& errorCode);
	bool takeUdpReceivedData(int fd, std::vector<char>& data, Address& srcAddr, DWORD& errorCode);
	bool queueTcpSend(int fd, const void* data, int len);
	bool queueUdpSend(int fd, const void* data, int len, const Address& dstAddr);
	bool hasPendingSend(int fd) const;

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

	enum Operation
	{
		OP_ACCEPT = 0,
		OP_TCP_RECV,
		OP_UDP_RECV,
		OP_TCP_SEND,
		OP_UDP_SEND
	};

	struct IocpContext
	{
		IocpContext(int fdArg, KBESOCKET socketArg, SocketKind kindArg, Operation operationArg, uint64 generationArg);

		OVERLAPPED overlapped;
		int fd;
		KBESOCKET socket;
		SocketKind kind;
		Operation operation;
		uint64 generation;
		WSABUF buffer;
		DWORD flags;
		std::vector<char> data;
		KBESOCKET acceptSocket;
		char acceptBuffer[(sizeof(sockaddr_in) + 16) * 2];
		sockaddr_in udpAddr;
		int udpAddrLen;
	};

	struct TcpReceivedData
	{
		std::vector<char> data;
		bool disconnected;
		DWORD errorCode;
	};

	struct UdpReceivedData
	{
		std::vector<char> data;
		Address srcAddr;
		DWORD errorCode;
	};

	struct PendingUdpSend
	{
		std::vector<char> data;
		sockaddr_in dstAddr;
	};

	struct SocketState
	{
		explicit SocketState(KBESOCKET socketArg);

		KBESOCKET socket;
		SocketKind kind;
		bool associated;
		bool registeredRead;
		uint64 generation;
		IocpContext* pPendingReadContext;
		IocpContext* pPendingWriteContext;
		std::deque<std::vector<char> > pendingTcpSends;
		size_t pendingTcpSendBytes;
		std::deque<PendingUdpSend> pendingUdpSends;
		LPFN_ACCEPTEX acceptExFn;
	};

	typedef std::unique_ptr<SocketState> SocketStatePtr;
	typedef std::map<int, SocketStatePtr> SocketStates;
	typedef std::deque<KBESOCKET> AcceptedSockets;
	typedef std::map<int, AcceptedSockets> AcceptedSocketMap;
	typedef std::deque<TcpReceivedData> TcpReceivedQueue;
	typedef std::map<int, TcpReceivedQueue> TcpReceivedMap;
	typedef std::deque<UdpReceivedData> UdpReceivedQueue;
	typedef std::map<int, UdpReceivedQueue> UdpReceivedMap;

	SocketState& socketStateForFd(int fd);
	bool ensureAssociated(SocketState& state, int fd);
	bool ensureReadArmed(int fd, SocketState& state);
	bool armTcpRead(int fd, SocketState& state);
	bool armUdpRead(int fd, SocketState& state);
	bool armTcpSend(int fd, SocketState& state);
	bool armUdpSend(int fd, SocketState& state);
	bool armAccept(int fd, SocketState& state);
	bool tryDetermineSocketKind(KBESOCKET socket, SocketKind& kind) const;
	bool loadAcceptEx(SocketState& state);
	void cleanupContext(IocpContext& context);
	void handleCompletion(ULONG_PTR completionKey, LPOVERLAPPED overlapped, DWORD bytesTransferred, bool success, DWORD errorCode);
	void cleanupStateIfUnused(int fd);
	void closeAcceptedSockets(AcceptedSockets& acceptedSockets);

	HANDLE completionPort_;
	SocketStates socketStates_;
	AcceptedSocketMap acceptedSockets_;
	TcpReceivedMap tcpReceived_;
	UdpReceivedMap udpReceived_;
};

}
}

#endif // KBE_PLATFORM == PLATFORM_WIN32

#endif // KBE_IOCP_POLLER_H
