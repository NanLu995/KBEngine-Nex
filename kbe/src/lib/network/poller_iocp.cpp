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
inline DWORD toTimeoutMilliseconds(double maxWait, bool hasWriteHandlers)
{
	double waitSeconds = maxWait;

	if (hasWriteHandlers)
	{
		waitSeconds = std::min(waitSeconds, 0.01);
	}

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

IocpPoller::IocpContext::IocpContext(int fdArg, KBESOCKET socketArg, SocketKind kindArg, uint64 generationArg) :
	overlapped(),
	fd(fdArg),
	socket(socketArg),
	kind(kindArg),
	generation(generationArg),
	buffer(),
	flags(0),
	probeByte(0),
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
	pPendingContext(NULL),
	acceptExFn(NULL)
{
}

//-------------------------------------------------------------------------------------
IocpPoller::IocpPoller() :
	EventPoller(),
	completionPort_(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)),
	socketStates_(),
	acceptedSockets_()
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
		if (state.pPendingContext != NULL)
		{
			CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.pPendingContext->overlapped);
			cleanupContext(*state.pPendingContext);
			delete state.pPendingContext;
			state.pPendingContext = NULL;
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
	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_TCP, state.generation);
	pContext->buffer.buf = &pContext->probeByte;
	pContext->buffer.len = 0;

	DWORD bytes = 0;
	DWORD flags = 0;
	int ret = WSARecv(state.socket, &pContext->buffer, 1, &bytes, &flags, &pContext->overlapped, NULL);
	if (ret == 0)
	{
		state.pPendingContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pPendingContext = pContext;
		return true;
	}

	delete pContext;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armUdpRead(int fd, SocketState& state)
{
	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_UDP, state.generation);
	pContext->flags = MSG_PEEK;
	pContext->buffer.buf = &pContext->probeByte;
	pContext->buffer.len = 1;

	DWORD bytes = 0;
	int ret = WSARecvFrom(state.socket, &pContext->buffer, 1, &bytes, &pContext->flags,
		reinterpret_cast<sockaddr*>(&pContext->udpAddr), &pContext->udpAddrLen,
		&pContext->overlapped, NULL);

	if (ret == 0)
	{
		state.pPendingContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pPendingContext = pContext;
		return true;
	}

	if (wsaErr == WSAEMSGSIZE)
	{
		// A 1-byte MSG_PEEK probe can fail synchronously when a larger UDP
		// datagram is already queued. That still means the socket is readable.
		delete pContext;
		this->triggerRead(fd);

		auto iter = socketStates_.find(fd);
		if (iter != socketStates_.end())
		{
			SocketState& currentState = *iter->second;
			if (currentState.registeredRead && currentState.pPendingContext == NULL)
			{
				ensureReadArmed(fd, currentState);
			}
		}

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

	IocpContext* pContext = new IocpContext(fd, state.socket, SOCKET_KIND_LISTENER, state.generation);

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
		state.pPendingContext = pContext;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == ERROR_IO_PENDING)
	{
		state.pPendingContext = pContext;
		return true;
	}

	cleanupContext(*pContext);
	delete pContext;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::ensureReadArmed(int fd, SocketState& state)
{
	if (!state.registeredRead || state.pPendingContext != NULL)
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
	if (iter == socketStates_.end())
	{
		SocketStatePtr state(new SocketState(static_cast<KBESOCKET>(fd)));
		iter = socketStates_.insert(std::make_pair(fd, std::move(state))).first;
	}

	SocketState& state = *iter->second;
	state.socket = static_cast<KBESOCKET>(fd);
	state.kind = SOCKET_KIND_UNKNOWN;
	state.associated = false;
	state.registeredRead = true;
	++state.generation;
	state.pPendingContext = NULL;
	state.acceptExFn = NULL;
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

	if (state.pPendingContext != NULL)
	{
		CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.pPendingContext->overlapped);
	}
	else
	{
		cleanupStateIfUnused(fd);
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::doDeregisterForWrite(int fd)
{
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
	if (state.registeredRead || state.pPendingContext != NULL || this->findForWrite(fd) != NULL)
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
void IocpPoller::handleCompletion(ULONG_PTR completionKey, LPOVERLAPPED overlapped, bool success, DWORD errorCode)
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
	const bool isCurrentContext = (pState != NULL &&
		pState->socket == pContext->socket &&
		pState->generation == pContext->generation &&
		pState->pPendingContext == pContext);
	const bool isUdpProbeMoreData = (pContext->kind == SOCKET_KIND_UDP && errorCode == ERROR_MORE_DATA);
	const bool isUdpPortUnreachable = (pContext->kind == SOCKET_KIND_UDP && errorCode == ERROR_PORT_UNREACHABLE);

	if (isCurrentContext)
	{
		pState->pPendingContext = NULL;
	}

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
		cleanupContext(*pContext);
		delete pContext;
		return;
	}

	if (pContext->kind == SOCKET_KIND_LISTENER)
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
	else
	{
		// UDP probe reads use a 1-byte MSG_PEEK buffer. A larger datagram
		// completes as ERROR_MORE_DATA, which still means "socket readable".
		if (!success && errorCode != 0 && !isUdpProbeMoreData && !isUdpPortUnreachable)
		{
			WARNING_MSG(fmt::format("IocpPoller::handleCompletion: read completion failed on fd {}: {}\n",
				fd, kbe_strerror(errorCode)));
		}

		this->triggerRead(fd);
	}

	cleanupContext(*pContext);
	delete pContext;

	auto currentIter = socketStates_.find(fd);
	if (currentIter != socketStates_.end())
	{
		SocketState& currentState = *currentIter->second;
		if (currentState.registeredRead && currentState.pPendingContext == NULL)
		{
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
		if (item.second->registeredRead && item.second->pPendingContext == NULL)
		{
			ensureReadArmed(item.first, *item.second);
		}
	}

	const bool hasWriteHandlers = !this->fdWriteHandlers().empty();
	DWORD timeoutMs = toTimeoutMilliseconds(maxWait, hasWriteHandlers);

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
		++readyCount;
		handleCompletion(completionKey, overlapped, ok == TRUE, errorCode);

		while (true)
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
			handleCompletion(completionKey, overlapped, ok == TRUE, errorCode);
		}
	}
	else if (!ok && errorCode != WAIT_TIMEOUT)
	{
		WARNING_MSG(fmt::format("IocpPoller::processPendingEvents: GetQueuedCompletionStatus failed: {}\n",
			kbe_strerror(errorCode)));
	}

	if (!this->fdWriteHandlers().empty())
	{
		std::vector<int> writeFds;
		writeFds.reserve(this->fdWriteHandlers().size());
		for (const auto& item : this->fdWriteHandlers())
		{
			writeFds.push_back(item.first);
		}

		for (int fd : writeFds)
		{
			++readyCount;
			this->triggerWrite(fd);
		}
	}

	return readyCount;
}

}
}

#endif // KBE_PLATFORM == PLATFORM_WIN32
