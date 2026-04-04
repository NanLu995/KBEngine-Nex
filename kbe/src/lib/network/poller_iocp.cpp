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

//-------------------------------------------------------------------------------------
IocpPoller::SocketState::SocketState(KBESOCKET socketArg) :
	overlapped(),
	socket(socketArg),
	kind(SOCKET_KIND_UNKNOWN),
	associated(false),
	registeredRead(false),
	pendingRead(false),
	acceptSocket(INVALID_SOCKET),
	acceptExFn(NULL),
	probeByte(0),
	acceptBuffer(),
	udpAddr(),
	udpAddrLen(sizeof(udpAddr))
{
	memset(&overlapped, 0, sizeof(overlapped));
	memset(&udpAddr, 0, sizeof(udpAddr));
	memset(acceptBuffer, 0, sizeof(acceptBuffer));
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
		if (state.pendingRead)
		{
			CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.overlapped);
		}

		if (state.acceptSocket != INVALID_SOCKET)
		{
			closesocket(state.acceptSocket);
			state.acceptSocket = INVALID_SOCKET;
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
bool IocpPoller::armTcpRead(SocketState& state)
{
	DWORD flags = 0;
	DWORD bytes = 0;
	WSABUF buffer;
	buffer.buf = &state.probeByte;
	buffer.len = 0;

	memset(&state.overlapped, 0, sizeof(state.overlapped));
	int ret = WSARecv(state.socket, &buffer, 1, &bytes, &flags, &state.overlapped, NULL);
	if (ret == 0)
	{
		state.pendingRead = true;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pendingRead = true;
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armUdpRead(SocketState& state)
{
	DWORD flags = MSG_PEEK;
	DWORD bytes = 0;
	WSABUF buffer;
	buffer.buf = &state.probeByte;
	buffer.len = 1;
	state.udpAddrLen = sizeof(state.udpAddr);
	memset(&state.udpAddr, 0, sizeof(state.udpAddr));
	memset(&state.overlapped, 0, sizeof(state.overlapped));

	int ret = WSARecvFrom(state.socket, &buffer, 1, &bytes, &flags,
		reinterpret_cast<sockaddr*>(&state.udpAddr), &state.udpAddrLen,
		&state.overlapped, NULL);

	if (ret == 0)
	{
		state.pendingRead = true;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == WSA_IO_PENDING)
	{
		state.pendingRead = true;
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::armAccept(SocketState& state)
{
	if (!loadAcceptEx(state))
	{
		return false;
	}

	if (state.acceptSocket != INVALID_SOCKET)
	{
		closesocket(state.acceptSocket);
		state.acceptSocket = INVALID_SOCKET;
	}

	state.acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (state.acceptSocket == INVALID_SOCKET)
	{
		ERROR_MSG(fmt::format("IocpPoller::armAccept: WSASocket failed: {}\n",
			kbe_strerror(WSAGetLastError())));
		return false;
	}

	DWORD bytes = 0;
	memset(&state.overlapped, 0, sizeof(state.overlapped));
	BOOL ok = state.acceptExFn(state.socket, state.acceptSocket,
		state.acceptBuffer, 0,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		&bytes, &state.overlapped);

	if (ok)
	{
		state.pendingRead = true;
		return true;
	}

	int wsaErr = WSAGetLastError();
	if (wsaErr == ERROR_IO_PENDING)
	{
		state.pendingRead = true;
		return true;
	}

	closesocket(state.acceptSocket);
	state.acceptSocket = INVALID_SOCKET;
	return false;
}

//-------------------------------------------------------------------------------------
bool IocpPoller::ensureReadArmed(int fd, SocketState& state)
{
	if (!state.registeredRead || state.pendingRead)
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
		return armTcpRead(state);
	case SOCKET_KIND_UDP:
		return armUdpRead(state);
	case SOCKET_KIND_LISTENER:
		return armAccept(state);
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

	iter->second->socket = static_cast<KBESOCKET>(fd);
	iter->second->registeredRead = true;
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

	if (state.pendingRead)
	{
		CancelIoEx(reinterpret_cast<HANDLE>(state.socket), &state.overlapped);
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
	if (state.registeredRead || state.pendingRead || this->findForWrite(fd) != NULL)
	{
		return;
	}

	if (state.acceptSocket != INVALID_SOCKET)
	{
		closesocket(state.acceptSocket);
		state.acceptSocket = INVALID_SOCKET;
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
void IocpPoller::handleCompletion(ULONG_PTR completionKey, LPOVERLAPPED overlapped, bool success, DWORD errorCode)
{
	if (overlapped == NULL)
	{
		return;
	}

	int fd = static_cast<int>(completionKey);
	auto iter = socketStates_.find(fd);
	if (iter == socketStates_.end())
	{
		return;
	}

	SocketState& state = *iter->second;
	state.pendingRead = false;

	if (!success && errorCode == ERROR_OPERATION_ABORTED)
	{
		cleanupStateIfUnused(fd);
		return;
	}

	if (state.kind == SOCKET_KIND_LISTENER)
	{
		if (success && state.acceptSocket != INVALID_SOCKET)
		{
			setsockopt(state.acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
				reinterpret_cast<const char*>(&state.socket), sizeof(state.socket));

			acceptedSockets_[fd].push_back(state.acceptSocket);
			state.acceptSocket = INVALID_SOCKET;
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
		if (!success && errorCode != 0)
		{
			WARNING_MSG(fmt::format("IocpPoller::handleCompletion: read completion failed on fd {}: {}\n",
				fd, kbe_strerror(errorCode)));
		}

		this->triggerRead(fd);
	}

	if (state.registeredRead)
	{
		ensureReadArmed(fd, state);
	}
	else
	{
		cleanupStateIfUnused(fd);
	}
}

//-------------------------------------------------------------------------------------
int IocpPoller::processPendingEvents(double maxWait)
{
	for (auto& item : socketStates_)
	{
		if (item.second->registeredRead && !item.second->pendingRead)
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
