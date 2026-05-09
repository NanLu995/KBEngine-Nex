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
		// IOCP 本身只返回 OVERLAPPED 完成事件，这里用 operation 标记
		// 本次完成到底属于 accept/recv/send 的哪条路径，避免再回到
		// select/readiness 模型里二次判断 socket 是否可读可写。
		OP_ACCEPT = 0,
		OP_TCP_RECV,
		OP_UDP_RECV,
		OP_TCP_SEND,
		OP_UDP_SEND
	};

	struct IocpContext
	{
		IocpContext(int fdArg, KBESOCKET socketArg, SocketKind kindArg, Operation operationArg, uint64 generationArg);

		// 每一次异步调用都拥有独立的 OVERLAPPED 和数据缓冲。
		// 完成回调回来前，buffer 必须一直有效，所以不能使用栈内存。
		OVERLAPPED overlapped;
		int fd;
		KBESOCKET socket;
		SocketKind kind;
		Operation operation;
		// generation 用来识别“旧 socket 的迟到完成事件”。
		// fd 在 Windows 上可能被复用，如果 channel 已经销毁又创建了新连接，
		// 旧 completion 不能误投递到新连接上。
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
		// IOCP 收到数据后先放入队列，再通过 triggerRead 让原来的
		// TCPPacketReceiver 消费。这样上层仍然沿用原来的消息分发路径，
		// 但底层 recv 已经变成 completion 驱动。
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
		// 每个 fd 同时只允许挂一个读类请求和一个写类请求。
		// 多投递虽然 IOCP 支持，但当前上层 PacketReceiver/PacketSender
		// 假定单线程、按序消费；这里保持单 pending 能减少乱序和销毁竞态。
		IocpContext* pPendingReadContext;
		IocpContext* pPendingWriteContext;
		// TCP 发送队列保存上层已经交给 IOCP、但还没完成发送的数据。
		// completion 回来后再触发原来的写完成流程，保证 Channel::FLAG_SENDING
		// 的生命周期和旧实现保持一致。
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
	// listener 必须在 listen() 之后才能被识别为 SOCKET_KIND_LISTENER。
	// 如果过早注册，SO_ACCEPTCONN 还不是 true，会被误判成普通 TCP。
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
	uint64 lastCompletionBudgetWarningTime_;
};

}
}

#endif // KBE_PLATFORM == PLATFORM_WIN32

#endif // KBE_IOCP_POLLER_H
