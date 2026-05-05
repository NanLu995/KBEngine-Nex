// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#include "poller_iocp.h"

#if KBE_PLATFORM == PLATFORM_WIN32

#include "helper/profile.h"
#include <cmath>

namespace KBEngine {
namespace
{
ProfileVal g_iocpIdleProfile("Idle");
}

namespace Network
{

namespace
{
const size_t IOCP_TCP_SEND_BATCH_BYTES = 64 * 1024;
const size_t IOCP_TCP_SEND_BACKLOG_BYTES = 1024 * 1024;
// 预算告警只是诊断“主循环被 completion 回调拖太久”，不是限流开关。
// 真正的处理上限来自配置里的 g_maxCompletionsPerTick / g_maxCompletionProcessingTimeMS。
const uint64 COMPLETION_BUDGET_WARNING_INTERVAL = 10 * stampsPerSecond();
const uint32 COMPLETION_BUDGET_WARNING_MULTIPLIER = 10;

inline DWORD toTimeoutMilliseconds(double maxWait)
{
	double waitSeconds = maxWait;

	if (waitSeconds <= 0.0)
	{
		return 0;
	}

	double milliseconds = std::ceil(waitSeconds * 1000.0);
	if (milliseconds > static_cast<double>(INFINITE - 1))
	{
		return INFINITE - 1;
	}

	return static_cast<DWORD>(milliseconds);
}
}

IocpPoller::IocpContext::IocpContext(int fdArg, KBESOCKET socketArg, SocketKind kindArg, Operation operationArg, uint64 generationArg) :
	overlapped(),
	fd(fdArg),
	socket(socketArg),
	kind(kindArg),
	operation(operationArg),
	generation(generationArg),
	buffer(),
	flags(0),
	data(),
	acceptSocket(INVALID_SOCKET),
	acceptBuffer(),
	udpAddr(),
	udpAddrLen(sizeof(udpAddr))
{
	memset(&overlapped, 0, sizeof(overlapped));
	memset(&buffer, 0, sizeof(buffer));
	memset(&udpAddr, 0, sizeof(udpAddr));
	memset(acceptBuffer, 0, sizeof(acceptBuffer));
}

//-------------------------------------------------------------------------------------
IocpPoller::SocketState::SocketState(KBESOCKET socketArg) :
	socket(socketArg),
	kind(SOCKET_KIND_UNKNOWN),
	associated(false),
	registeredRead(false),
	generation(0),
	pPendingReadContext(NULL),
	pPendingWriteContext(NULL),
	pendingTcpSends(),
	pendingTcpSendBytes(0),
	pendingUdpSends(),
	acceptExFn(NULL)
{
}

//-------------------------------------------------------------------------------------
IocpPoller::IocpPoller() :
	EventPoller(),
	completionPort_(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)),
	socketStates_(),
	acceptedSockets_(),
	tcpReceived_(),
	udpReceived_(),
	lastCompletionBudgetWarningTime_(0)
{
	if (completionPort_ == NULL)
	{
		ERROR_MSG(fmt::format("IocpPoller::IocpPoller: CreateIoCompletionPort failed: {}\n",
			kbe_strerror(GetLastError())));
	}
}

//-------------------------------------------------------------------------------------
IocpPoller::~IocpPoller()
{
	for (auto& item : acceptedSockets_)
	{
		closeAcceptedSockets(item.second);
	}

	for (auto& item : socketStates_)
	{
		SocketState& state = *item.second;
		if (state.pPendingReadContext != NULL)
		{
			CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.pPendingReadContext->overlapped);
			cleanupContext(*state.pPendingReadContext);
			delete state.pPendingReadContext;
			state.pPendingReadContext = NULL;
		}

		if (state.pPendingWriteContext != NULL)
		{
			CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.pPendingWriteContext->overlapped);
			cleanupContext(*state.pPendingWriteContext);
			delete state.pPendingWriteContext;
			state.pPendingWriteContext = NULL;
		}
	}

	if (completionPort_ != NULL)
	{
		CloseHandle(completionPort_);
		completionPort_ = NULL;
	}
}

//-------------------------------------------------------------------------------------
bool IocpPoller::takeAcceptedSocket(int fd, KBESOCKET& acceptedSocket)
{
	auto iter = acceptedSockets_.find(fd);
	if (iter == acceptedSockets_.end() || iter->second.empty())
	{
		return false;
	}

	acceptedSocket = iter->second.front();
	iter->second.pop_front();

	if (iter->second.empty())
	{
		acceptedSockets_.erase(iter);
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::takeTcpReceivedData(int fd, std::vector<char>& data, bool& disconnected, DWORD& errorCode)
{
	auto iter = tcpReceived_.find(fd);
	if (iter == tcpReceived_.end() || iter->second.empty())
	{
		return false;
	}

	TcpReceivedData& item = iter->second.front();
	data.swap(item.data);
	disconnected = item.disconnected;
	errorCode = item.errorCode;
	iter->second.pop_front();

	if (iter->second.empty())
	{
		tcpReceived_.erase(iter);
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::takeUdpReceivedData(int fd, std::vector<char>& data, Address& srcAddr, DWORD& errorCode)
{
	auto iter = udpReceived_.find(fd);
	if (iter == udpReceived_.end() || iter->second.empty())
	{
		return false;
	}

	UdpReceivedData& item = iter->second.front();
	data.swap(item.data);
	srcAddr = item.srcAddr;
	errorCode = item.errorCode;
	iter->second.pop_front();

	if (iter->second.empty())
	{
		udpReceived_.erase(iter);
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::queueTcpSend(int fd, const void* data, int len)
{
	if (len <= 0)
	{
		return true;
	}

	SocketState& state = socketStateForFd(fd);
	if (state.kind == SOCKET_KIND_UNKNOWN)
	{
		state.kind = SOCKET_KIND_TCP;
	}

	// 这里给 IOCP 自己的发送积压设置硬上限。
	// Channel 层还有 send-window 检查，但 IOCP completion 模型里，
	// 数据会先进入 pendingTcpSends 等待 WSASend 完成；如果没有这个上限，
	// 对端卡住时内存会绕过旧的同步 send 压力反馈继续增长。
	if (state.pendingTcpSendBytes + static_cast<size_t>(len) > IOCP_TCP_SEND_BACKLOG_BYTES)
	{
		WSASetLastError(WSAEWOULDBLOCK);
		return false;
	}

	std::vector<char> sendData(static_cast<const char*>(data), static_cast<const char*>(data) + len);
	state.pendingTcpSendBytes += sendData.size();
	state.pendingTcpSends.push_back(std::move(sendData));

	// 如果当前没有挂起的写请求，立即投递 WSASend；
	// 如果已有写请求，则只入队，等发送 completion 后继续 armTcpSend。
	return armTcpSend(fd, state);
}

//-------------------------------------------------------------------------------------
bool IocpPoller::queueUdpSend(int fd, const void* data, int len, const Address& dstAddr)
{
	if (len <= 0)
	{
		return true;
	}

	SocketState& state = socketStateForFd(fd);
	state.kind = SOCKET_KIND_UDP;

	PendingUdpSend pending;
	pending.data.assign(static_cast<const char*>(data), static_cast<const char*>(data) + len);
	memset(&pending.dstAddr, 0, sizeof(pending.dstAddr));
	pending.dstAddr.sin_family = AF_INET;
	pending.dstAddr.sin_addr.s_addr = dstAddr.ip;
	pending.dstAddr.sin_port = dstAddr.port;
	state.pendingUdpSends.push_back(std::move(pending));

	// UDP 也走 completion，避免 Windows 下 KCP/UDP 发送路径退回同步 sendto。
	return armUdpSend(fd, state);
}

//-------------------------------------------------------------------------------------
bool IocpPoller::hasPendingSend(int fd) const
{
	auto iter = socketStates_.find(fd);
	if (iter == socketStates_.end())
	{
		return false;
	}

	const SocketState& state = *iter->second;
	return state.pPendingWriteContext != NULL || !state.pendingTcpSends.empty() ||
		state.pendingTcpSendBytes > 0 || !state.pendingUdpSends.empty();
}

//-------------------------------------------------------------------------------------
IocpPoller::SocketState& IocpPoller::socketStateForFd(int fd)
{
	auto iter = socketStates_.find(fd);
	if (iter == socketStates_.end())
	{
		SocketStatePtr state(new SocketState(static_cast<KBESOCKET>(fd)));
		iter = socketStates_.insert(std::make_pair(fd, std::move(state))).first;
	}

	SocketState& state = *iter->second;
	state.socket = static_cast<KBESOCKET>(fd);
	return state;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::ensureAssociated(SocketState& state, int fd)
{
	if (state.associated)
	{
		return true;
	}

	HANDLE handle = CreateIoCompletionPort(reinterpret_cast<HANDLE>(state.socket), completionPort_, static_cast<ULONG_PTR>(fd), 0);
	if (handle == NULL)
	{
		ERROR_MSG(fmt::format("IocpPoller::ensureAssociated: CreateIoCompletionPort failed for fd {}: {}\n",
			fd, kbe_strerror(GetLastError())));
		return false;
	}

	state.associated = true;
	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::tryDetermineSocketKind(KBESOCKET socket, SocketKind& kind) const
{
	int socketType = 0;
	int acceptConn = 0;
	int socketTypeLen = sizeof(socketType);
	int acceptConnLen = sizeof(acceptConn);

	if (getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&socketType), &socketTypeLen) != 0)
	{
		return false;
	}

	if (socketType == SOCK_DGRAM)
	{
		kind = SOCKET_KIND_UDP;
		return true;
	}

	if (getsockopt(socket, SOL_SOCKET, SO_ACCEPTCONN, reinterpret_cast<char*>(&acceptConn), &acceptConnLen) == 0 && acceptConn != 0)
	{
		kind = SOCKET_KIND_LISTENER;
		return true;
	}

	if (socketType == SOCK_STREAM)
	{
		kind = SOCKET_KIND_TCP;
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::loadAcceptEx(SocketState& state)
{
	if (state.acceptExFn != NULL)
	{
		return true;
	}

	DWORD bytes = 0;
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	if (WSAIoctl(state.socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx),
		&state.acceptExFn, sizeof(state.acceptExFn),
		&bytes, NULL, NULL) != 0)
	{
		ERROR_MSG(fmt::format("IocpPoller::loadAcceptEx: WSAIoctl failed: {}\n",
			kbe_strerror(WSAGetLastError())));
		state.acceptExFn = NULL;
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armTcpRead(int fd, SocketState& state)
{
	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_TCP, OP_TCP_RECV, state.generation);
	pContext->data.resize(PACKET_MAX_SIZE_TCP);
	pContext->buffer.buf = pContext->data.data();
	pContext->buffer.len = static_cast<ULONG>(pContext->data.size());

	DWORD bytes = 0;
	DWORD flags = 0;
	int ret = WSARecv(state.socket, &pContext->buffer, 1, &bytes, &flags, &pContext->overlapped, NULL);
	if (ret == 0)
	{
		state.pPendingReadContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pPendingReadContext = pContext;
		return true;
	}

	delete pContext;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armUdpRead(int fd, SocketState& state)
{
	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_UDP, OP_UDP_RECV, state.generation);
	pContext->flags = 0;
	pContext->data.resize(PACKET_MAX_SIZE_UDP);
	pContext->buffer.buf = pContext->data.data();
	pContext->buffer.len = static_cast<ULONG>(pContext->data.size());

	DWORD bytes = 0;
	int ret = WSARecvFrom(state.socket, &pContext->buffer, 1, &bytes, &pContext->flags,
		reinterpret_cast<sockaddr*>(&pContext->udpAddr), &pContext->udpAddrLen,
		&pContext->overlapped, NULL);

	if (ret == 0)
	{
		state.pPendingReadContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pPendingReadContext = pContext;
		return true;
	}

	delete pContext;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armTcpSend(int fd, SocketState& state)
{
	if (state.pPendingWriteContext != NULL || state.pendingTcpSends.empty())
	{
		return true;
	}

	if (!ensureAssociated(state, fd))
	{
		return false;
	}

	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_TCP, OP_TCP_SEND, state.generation);
	size_t batchBytes = 0;
	// 合并多个小包为一次 WSASend，减少 IOCP completion 数量。
	// 不能无限合并，否则一次 completion 回调可能占用过久，也会增加
	// 断线时被取消的 outstanding buffer 大小。
	while (!state.pendingTcpSends.empty() && batchBytes < IOCP_TCP_SEND_BATCH_BYTES)
	{
		std::vector<char>& front = state.pendingTcpSends.front();
		const size_t bytesToAppend = std::min(front.size(), IOCP_TCP_SEND_BATCH_BYTES - batchBytes);
		pContext->data.insert(pContext->data.end(), front.begin(), front.begin() + bytesToAppend);
		batchBytes += bytesToAppend;
		state.pendingTcpSendBytes -= bytesToAppend;

		if (bytesToAppend == front.size())
		{
			state.pendingTcpSends.pop_front();
		}
		else
		{
			front.erase(front.begin(), front.begin() + bytesToAppend);
		}
	}

	pContext->buffer.buf = pContext->data.data();
	pContext->buffer.len = static_cast<ULONG>(pContext->data.size());

	DWORD bytes = 0;
	int ret = WSASend(state.socket, &pContext->buffer, 1, &bytes, 0, &pContext->overlapped, NULL);
	if (ret == 0)
	{
		state.pPendingWriteContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pPendingWriteContext = pContext;
		return true;
	}

	delete pContext;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armUdpSend(int fd, SocketState& state)
{
	if (state.pPendingWriteContext != NULL || state.pendingUdpSends.empty())
	{
		return true;
	}

	if (!ensureAssociated(state, fd))
	{
		return false;
	}

	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_UDP, OP_UDP_SEND, state.generation);
	PendingUdpSend pending = std::move(state.pendingUdpSends.front());
	state.pendingUdpSends.pop_front();
	pContext->data.swap(pending.data);
	pContext->udpAddr = pending.dstAddr;
	pContext->udpAddrLen = sizeof(pContext->udpAddr);
	pContext->buffer.buf = pContext->data.data();
	pContext->buffer.len = static_cast<ULONG>(pContext->data.size());

	DWORD bytes = 0;
	int ret = WSASendTo(state.socket, &pContext->buffer, 1, &bytes, 0,
		reinterpret_cast<sockaddr*>(&pContext->udpAddr), pContext->udpAddrLen,
		&pContext->overlapped, NULL);
	if (ret == 0)
	{
		state.pPendingWriteContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pPendingWriteContext = pContext;
		return true;
	}

	delete pContext;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armAccept(int fd, SocketState& state)
{
	if (!loadAcceptEx(state))
	{
		return false;
	}

	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_LISTENER, OP_ACCEPT, state.generation);

	pContext->acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (pContext->acceptSocket == INVALID_SOCKET)
	{
		ERROR_MSG(fmt::format("IocpPoller::armAccept: WSASocket failed: {}\n",
			kbe_strerror(WSAGetLastError())));
		delete pContext;
		return false;
	}

	DWORD bytes = 0;
	BOOL ok = state.acceptExFn(state.socket, pContext->acceptSocket,
		pContext->acceptBuffer, 0,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		&bytes, &pContext->overlapped);

	if (ok)
	{
		state.pPendingReadContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == ERROR_IO_PENDING)
	{
		state.pPendingReadContext = pContext;
		return true;
	}

	WARNING_MSG(fmt::format("IocpPoller::armAccept: AcceptEx failed on fd {}: {}\n",
		fd, kbe_strerror(wsaErr)));
	cleanupContext(*pContext);
	delete pContext;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::ensureReadArmed(int fd, SocketState& state)
{
	if (!state.registeredRead || state.pPendingReadContext != NULL)
	{
		return true;
	}

	if (!ensureAssociated(state, fd))
	{
		return false;
	}

	SocketKind detectedKind = SOCKET_KIND_UNKNOWN;
	if (!tryDetermineSocketKind(state.socket, detectedKind))
	{
		return false;
	}

	state.kind = detectedKind;

	switch (state.kind)
	{
	case SOCKET_KIND_TCP:
		return armTcpRead(fd, state);
	case SOCKET_KIND_UDP:
		return armUdpRead(fd, state);
	case SOCKET_KIND_LISTENER:
		return armAccept(fd, state);
	default:
		return false;
	}
}

//-------------------------------------------------------------------------------------
bool IocpPoller::doRegisterForRead(int fd)
{
	auto iter = socketStates_.find(fd);
	bool isNewState = false;
	if (iter == socketStates_.end())
	{
		SocketStatePtr state(new SocketState(static_cast<KBESOCKET>(fd)));
		iter = socketStates_.insert(std::make_pair(fd, std::move(state))).first;
		isNewState = true;
	}

	SocketState& state = *iter->second;
	state.socket = static_cast<KBESOCKET>(fd);
	state.registeredRead = true;

	// fd 重新注册且没有任何 outstanding IO 时，视为一个新的 socket 生命周期。
	// 清理旧的 completion 队列并递增 generation，防止 Windows 复用 fd 后，
	// 旧连接迟到的 completion 被新连接消费。
	if (isNewState ||
		(state.pPendingReadContext == NULL && state.pPendingWriteContext == NULL &&
			state.pendingTcpSends.empty() && state.pendingTcpSendBytes == 0 && state.pendingUdpSends.empty()))
	{
		tcpReceived_.erase(fd);
		udpReceived_.erase(fd);
		state.kind = SOCKET_KIND_UNKNOWN;
		state.associated = false;
		++state.generation;
		state.acceptExFn = NULL;
	}

	ensureReadArmed(fd, *iter->second);
	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::doRegisterForWrite(int fd)
{
	(void)fd;
	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::doDeregisterForRead(int fd)
{
	auto iter = socketStates_.find(fd);
	if (iter == socketStates_.end())
	{
		return false;
	}

	SocketState& state = *iter->second;
	state.registeredRead = false;
	tcpReceived_.erase(fd);
	udpReceived_.erase(fd);

	// 读注销表示 channel/socket 生命周期结束，读写 outstanding IO 都要取消。
	// CancelIoEx 后 completion 仍可能异步返回，所以这里只断开 state 引用，
	// 真正的 context 内存在 handleCompletion 收到 ERROR_OPERATION_ABORTED 后释放。
	if (state.pPendingReadContext != NULL)
	{
		CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.pPendingReadContext->overlapped);
		state.pPendingReadContext = NULL;
	}

	if (state.pPendingWriteContext != NULL)
	{
		CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.pPendingWriteContext->overlapped);
		state.pPendingWriteContext = NULL;
	}

	state.pendingTcpSends.clear();
	state.pendingTcpSendBytes = 0;
	state.pendingUdpSends.clear();
	++state.generation;

	cleanupStateIfUnused(fd);

	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::doDeregisterForWrite(int fd)
{
	auto iter = socketStates_.find(fd);
	if (iter == socketStates_.end())
	{
		return true;
	}

	SocketState& state = *iter->second;
	state.pendingTcpSends.clear();
	state.pendingTcpSendBytes = 0;
	state.pendingUdpSends.clear();

	// 写注销只停止发送队列，不能清 tcpReceived_/udpReceived_。
	// completion 线程在 handleCompletion 中会先把 recv 数据入队再 triggerRead；
	// 如果 onSendCompleted/stopSend 清掉接收队列，登录/断线消息会被吞掉。
	if (state.pPendingWriteContext != NULL)
	{
		CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.pPendingWriteContext->overlapped);
		state.pPendingWriteContext = NULL;
	}

	cleanupStateIfUnused(fd);
	return true;
}

//-------------------------------------------------------------------------------------
void IocpPoller::cleanupStateIfUnused(int fd)
{
	auto iter = socketStates_.find(fd);
	if (iter == socketStates_.end())
	{
		return;
	}

	SocketState& state = *iter->second;
	if (state.registeredRead || state.pPendingReadContext != NULL || state.pPendingWriteContext != NULL ||
		!state.pendingTcpSends.empty() || state.pendingTcpSendBytes > 0 ||
		!state.pendingUdpSends.empty() || this->findForWrite(fd) != NULL)
	{
		return;
	}

	auto acceptedIter = acceptedSockets_.find(fd);
	if (acceptedIter != acceptedSockets_.end())
	{
		closeAcceptedSockets(acceptedIter->second);
		acceptedSockets_.erase(acceptedIter);
	}

	socketStates_.erase(iter);
}

//-------------------------------------------------------------------------------------
void IocpPoller::closeAcceptedSockets(AcceptedSockets& acceptedSockets)
{
	while (!acceptedSockets.empty())
	{
		KBESOCKET acceptedSocket = acceptedSockets.front();
		acceptedSockets.pop_front();
		if (acceptedSocket != INVALID_SOCKET)
		{
			closesocket(acceptedSocket);
		}
	}
}

//-------------------------------------------------------------------------------------
void IocpPoller::cleanupContext(IocpContext& context)
{
	if (context.acceptSocket != INVALID_SOCKET)
	{
		closesocket(context.acceptSocket);
		context.acceptSocket = INVALID_SOCKET;
	}
}

//-------------------------------------------------------------------------------------
void IocpPoller::handleCompletion(ULONG_PTR completionKey, LPOVERLAPPED overlapped, DWORD bytesTransferred, bool success, DWORD errorCode)
{
	if (overlapped == NULL)
	{
		return;
	}

	(void)completionKey;
	IocpContext* pContext = reinterpret_cast<IocpContext*>(overlapped);
	const int fd = pContext->fd;

	auto iter = socketStates_.find(fd);
	const bool hasState = (iter != socketStates_.end());
	SocketState* pState = hasState ? iter->second.get() : NULL;
	IocpContext** ppCurrentContext = NULL;
	if (pState != NULL)
	{
		ppCurrentContext = (pContext->operation == OP_TCP_SEND || pContext->operation == OP_UDP_SEND) ?
			&pState->pPendingWriteContext : &pState->pPendingReadContext;
	}

	const bool isCurrentContext = (pState != NULL &&
		pState->socket == pContext->socket &&
		pState->generation == pContext->generation &&
		ppCurrentContext != NULL &&
		*ppCurrentContext == pContext);
	const bool isUdpPortUnreachable = (pContext->kind == SOCKET_KIND_UDP && errorCode == ERROR_PORT_UNREACHABLE);

	if (isCurrentContext)
	{
		*ppCurrentContext = NULL;
	}

	// 取消 IO 是正常注销路径，不算网络错误。
	// 这里仍然必须释放 context，因为 Windows 会为被取消的 OVERLAPPED
	// 投递一个完成包回来。
	if (!success && errorCode == ERROR_OPERATION_ABORTED)
	{
		cleanupContext(*pContext);
		delete pContext;

		if (isCurrentContext)
		{
			cleanupStateIfUnused(fd);
		}

		return;
	}

	if (!isCurrentContext)
	{
		// 迟到的 completion：fd/socket/generation/context 任一不匹配都丢弃。
		// 这是 IOCP 下避免“旧连接事件打到新连接”的核心保护。
		cleanupContext(*pContext);
		delete pContext;
		return;
	}

	if (pContext->operation == OP_ACCEPT)
	{
		if (success && pContext->acceptSocket != INVALID_SOCKET)
		{
			setsockopt(pContext->acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
				reinterpret_cast<const char*>(&pContext->socket), sizeof(pContext->socket));

			acceptedSockets_[fd].push_back(pContext->acceptSocket);
			pContext->acceptSocket = INVALID_SOCKET;
			this->triggerRead(fd);
		}
		else if (!success)
		{
			WARNING_MSG(fmt::format("IocpPoller::handleCompletion: AcceptEx failed on fd {}: {}\n",
				fd, kbe_strerror(errorCode)));
		}
	}
	else if (pContext->operation == OP_TCP_RECV)
	{
		// TCP_RECV completion 不直接调用 recv。
		// 数据先进入 tcpReceived_，再 triggerRead，让 TCPPacketReceiver
		// 通过原有 PacketReader/Channel 逻辑解析消息。
		TcpReceivedData item;
		item.disconnected = (success && bytesTransferred == 0);
		item.errorCode = success ? 0 : errorCode;
		if (success && bytesTransferred > 0)
		{
			item.data.assign(pContext->data.begin(), pContext->data.begin() + bytesTransferred);
		}

		if (!success && errorCode != 0)
		{
			WARNING_MSG(fmt::format("IocpPoller::handleCompletion: read completion failed on fd {}: {}\n",
				fd, kbe_strerror(errorCode)));
		}

		tcpReceived_[fd].push_back(std::move(item));
		this->triggerRead(fd);
	}
	else if (pContext->operation == OP_UDP_RECV)
	{
		if (!success && errorCode != 0 && !isUdpPortUnreachable)
		{
			WARNING_MSG(fmt::format("IocpPoller::handleCompletion: udp recv completion failed on fd {}: {}\n",
				fd, kbe_strerror(errorCode)));
		}

		if (success && bytesTransferred > 0)
		{
			UdpReceivedData item;
			item.errorCode = 0;
			item.srcAddr.ip = pContext->udpAddr.sin_addr.s_addr;
			item.srcAddr.port = pContext->udpAddr.sin_port;
			item.data.assign(pContext->data.begin(), pContext->data.begin() + bytesTransferred);
			udpReceived_[fd].push_back(std::move(item));
			this->triggerRead(fd);
		}
		else if (!success && isUdpPortUnreachable)
		{
			UdpReceivedData item;
			item.errorCode = errorCode;
			udpReceived_[fd].push_back(std::move(item));
			this->triggerRead(fd);
		}
	}
	else if (pContext->operation == OP_TCP_SEND)
	{
		if (!success && errorCode != 0)
		{
			WARNING_MSG(fmt::format("IocpPoller::handleCompletion: send completion failed on fd {}: {}\n",
				fd, kbe_strerror(errorCode)));

			TcpReceivedData item;
			item.disconnected = false;
			item.errorCode = errorCode;
			tcpReceived_[fd].push_back(std::move(item));
			this->triggerRead(fd);

			cleanupContext(*pContext);
			delete pContext;
			return;
		}

		if (success && bytesTransferred < pContext->data.size())
		{
			// WSASend completion 允许只完成部分字节。
			// 未发送完的数据必须放回队首，保持 TCP 字节流顺序。
			std::vector<char> remaining(pContext->data.begin() + bytesTransferred, pContext->data.end());
			pState->pendingTcpSendBytes += remaining.size();
			pState->pendingTcpSends.push_front(std::move(remaining));
		}

		if (!pState->pendingTcpSends.empty())
		{
			armTcpSend(fd, *pState);
		}
		else if (this->findForWrite(fd) != NULL)
		{
			// 所有 IOCP 发送都完成后再触发写通知，让 TCPPacketSender
			// 调用 Channel::onSendCompleted()，保持旧的 FLAG_SENDING 生命周期。
			this->triggerWrite(fd);
		}
	}
	else if (pContext->operation == OP_UDP_SEND)
	{
		if (!success && errorCode != 0 && !isUdpPortUnreachable)
		{
			WARNING_MSG(fmt::format("IocpPoller::handleCompletion: udp send completion failed on fd {}: {}\n",
				fd, kbe_strerror(errorCode)));
		}

		if (!pState->pendingUdpSends.empty())
		{
			armUdpSend(fd, *pState);
		}
	}

	cleanupContext(*pContext);
	delete pContext;

	auto currentIter = socketStates_.find(fd);
	if (currentIter != socketStates_.end())
	{
		SocketState& currentState = *currentIter->second;
		if (currentState.registeredRead && currentState.pPendingReadContext == NULL)
		{
			// completion 回调可能销毁 channel 或重新注册 fd。
			// 因此重挂 read 前必须重新查当前 state，而不是继续使用旧指针。
			ensureReadArmed(fd, currentState);
		}
		else if (!currentState.registeredRead)
		{
			cleanupStateIfUnused(fd);
		}
	}
}

//-------------------------------------------------------------------------------------
int IocpPoller::processPendingEvents(double maxWait)
{
	for (auto& item : socketStates_)
	{
		if (item.second->registeredRead && item.second->pPendingReadContext == NULL)
		{
			ensureReadArmed(item.first, *item.second);
		}

		if (!item.second->pendingTcpSends.empty() && item.second->pPendingWriteContext == NULL)
		{
			armTcpSend(item.first, *item.second);
		}

		if (!item.second->pendingUdpSends.empty() && item.second->pPendingWriteContext == NULL)
		{
			armUdpSend(item.first, *item.second);
		}
	}

	DWORD timeoutMs = toTimeoutMilliseconds(maxWait);

#if ENABLE_WATCHERS
	g_iocpIdleProfile.start();
#else
	uint64 startTime = timestamp();
#endif

	KBEConcurrency::onStartMainThreadIdling();
	DWORD bytesTransferred = 0;
	ULONG_PTR completionKey = 0;
	LPOVERLAPPED overlapped = NULL;
	BOOL ok = GetQueuedCompletionStatus(completionPort_, &bytesTransferred, &completionKey, &overlapped, timeoutMs);
	DWORD errorCode = ok ? 0 : GetLastError();
	KBEConcurrency::onEndMainThreadIdling();

#if ENABLE_WATCHERS
	g_iocpIdleProfile.stop();
	spareTime_ += g_iocpIdleProfile.lastTime_;
#else
	spareTime_ += timestamp() - startTime;
#endif

	int readyCount = 0;

	if (overlapped != NULL)
	{
		const uint64 completionProcessingStart = timestamp();
		// completionBudget 是主线程公平性保护：
		// KBEngine 的网络 completion 会同步进入 PacketReader/Entity 逻辑，
		// 如果一次 tick 无限制 drain IOCP，断线或启动 burst 会把 timer、
		// app 心跳和其他 channel 处理饿住。预算到达后保留剩余 completion
		// 在下一轮 tick 继续取，IOCP 队列本身保证完成事件不会丢。
		const uint64 completionProcessingBudget =
			g_maxCompletionProcessingTimeMS > 0 ?
			(uint64(g_maxCompletionProcessingTimeMS) * stampsPerSecond() / 1000) : 0;

		++readyCount;
		handleCompletion(completionKey, overlapped, bytesTransferred, ok == TRUE, errorCode);

		while (readyCount < static_cast<int>(g_maxCompletionsPerTick) &&
			(completionProcessingBudget == 0 || timestamp() - completionProcessingStart < completionProcessingBudget))
		{
			bytesTransferred = 0;
			completionKey = 0;
			overlapped = NULL;
			ok = GetQueuedCompletionStatus(completionPort_, &bytesTransferred, &completionKey, &overlapped, 0);
			errorCode = ok ? 0 : GetLastError();

			if (overlapped == NULL)
			{
				break;
			}

			++readyCount;
			handleCompletion(completionKey, overlapped, bytesTransferred, ok == TRUE, errorCode);
		}

		const uint64 completionProcessingElapsed = timestamp() - completionProcessingStart;
		const bool countBudgetExhausted = readyCount >= static_cast<int>(g_maxCompletionsPerTick);
		const bool timeBudgetExceeded = completionProcessingBudget > 0 &&
			completionProcessingElapsed >= completionProcessingBudget;
		const bool timeBudgetWarningExceeded = completionProcessingBudget > 0 &&
			completionProcessingElapsed >= completionProcessingBudget * COMPLETION_BUDGET_WARNING_MULTIPLIER;

		// countBudgetExhausted 只代表本 tick 还有 completion 留到下 tick，
		// 对断线/启动 burst 很常见，所以不单独报警。
		// 只有处理时间达到预算多倍时才 warning，避免把正常登录、登出、
		// getCell 这类上层回调耗时误判为 IOCP 异常。
		if (timeBudgetWarningExceeded)
		{
			uint64 now = timestamp();
			if (lastCompletionBudgetWarningTime_ == 0 ||
				now - lastCompletionBudgetWarningTime_ >= COMPLETION_BUDGET_WARNING_INTERVAL)
			{
				lastCompletionBudgetWarningTime_ = now;
				WARNING_MSG(fmt::format("IocpPoller::processPendingEvents: completion processing took too long, count={}, countBudget={}, timeBudget={}, maxCount={}, maxTimeMS={}, elapsedMS={}\n",
					readyCount, countBudgetExhausted, timeBudgetExceeded, g_maxCompletionsPerTick,
					g_maxCompletionProcessingTimeMS,
					completionProcessingElapsed * 1000 / stampsPerSecond()));
			}
		}
	}
	else if (!ok && errorCode != WAIT_TIMEOUT)
	{
		WARNING_MSG(fmt::format("IocpPoller::processPendingEvents: GetQueuedCompletionStatus failed: {}\n",
			kbe_strerror(errorCode)));
	}

	return readyCount;
}

}
}

#endif // KBE_PLATFORM == PLATFORM_WIN32
