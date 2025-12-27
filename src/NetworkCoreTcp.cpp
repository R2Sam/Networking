#include "NetworkCoreTcp.h"

#include "Log/Log.h"
#include "Assert.h"
#include <chrono>
#include <thread>

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <netdb.h>
	#include <netinet/tcp.h>
#endif

#include <cstring>

struct PacketHeader
{
	u32 size;
};

NetworkCoreTcp::NetworkCoreTcp() 
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif
}

NetworkCoreTcp::~NetworkCoreTcp() 
{
	Shutdown();
}

bool NetworkCoreTcp::InitServer(const u16 port) 
{
	_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (_socket < 0)
	{
		LogColor(LOG_RED, "Failed to initalize socket");
		return false;
	}

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(_socket, (sockaddr*)&addr, sizeof(addr)) < 0)
	{
		LogColor(LOG_RED, "Failed to bind socket");
		return false;
	}

	if (listen(_socket, SOMAXCONN) < 0)
	{
		LogColor(LOG_RED, "Failed to listen socket");
		return false;
	}

	if (!SetSocketNonBlocking(_socket))
	{
		LogColor(LOG_RED, "Failed to set socket to non blocking");
		return false;
	}

	return true;
}

bool NetworkCoreTcp::InitClient() 
{
	return true;
}

void NetworkCoreTcp::Shutdown() 
{
	if (_socket >= 0)
	{
#ifdef _WIN32
        closesocket(_socket);
#else
        close(_socket);
#endif
        _socket = -1;
    }
    for (const PeerId& peer : _peerManager.GetAllPeerIds())
    {
        Disconnect(peer);
    }
}

PeerId NetworkCoreTcp::Connect(const Address& address) 
{
	sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    if (inet_pton(AF_INET, address.ip.c_str(), &addr.sin_addr) <= 0)
    {
    	addrinfo hints = {}, *res;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(address.ip.c_str(), nullptr, &hints, &res) != 0)
        {
        	LogColor(LOG_YELLOW, "Failed to parse ip for connection to ", address.ip, ":", address.port);

	  		return 0;
        }
    }

    i32 sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		LogColor(LOG_YELLOW, "Failed to initalize socket for connection to ", address.ip, ":", address.port);
		return 0;
	}

    if (!SetSocketNonBlocking(sock))
	{
		LogColor(LOG_YELLOW, "Failed to set socket to non blocking for connection to ", address.ip, ":", address.port);
		return false;
	}

    i64 rv = connect(sock, (sockaddr*)&addr, sizeof(addr));
#ifdef _WIN32
	i32 lastError = WSAGetLastError();
	if (rv != 0 && lastError != WSAEWOULDBLOCK)
	{
	    closesocket(sock);
	    LogColor(LOG_YELLOW, "Failed to connect to ", address.ip, ":", address.port);
	    return 0;
	}
#else
	if (rv != 0 && errno != EINPROGRESS)
	{
	    close(sock);
	    LogColor(LOG_YELLOW, "Failed to connect to ", address.ip, ":", address.port);
	    return 0;
	}
#endif

    PeerId id = _peerManager.AddPeer(sock, address, ConnectionState::Connecting);
    return id;
}

void NetworkCoreTcp::Disconnect(const PeerId peer) 
{
	Peer p = GetPeer(peer);
    if (p.socket < 0)
    {
    	return;
    }

    if (p.socket >= 0)
    {
#ifdef _WIN32
        closesocket(p.socket);
#else
        close(p.socket);
#endif
    }

    p.state = ConnectionState::Disconnecting;

    _peerManager.RemovePeer(peer);
}

void NetworkCoreTcp::Poll(std::queue<NetworkEvent>& events, const u32 minTimeMs) 
{
	auto startTime = std::chrono::steady_clock::now();

	if (_socket >= 0 )
	{
		while(true)
		{
			sockaddr_in addr = {};
			u32 len = sizeof(addr);

			i32 clientsocket = accept(_socket, (sockaddr*)&addr, &len);
			if (clientsocket < 0)
			{
				break;
			}

			char ip[32];
			inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));

            Address address = {ip, ntohs(addr.sin_port)};

            PeerId id = _peerManager.AddPeer(clientsocket, address, ConnectionState::Connected);

			if (SetSocketNonBlocking(clientsocket))
			{
            	events.emplace(NetworkEventType::Connect, id, 1, std::vector<u8>(), address);
			}

			else
			{
				LogColor(LOG_YELLOW, "Failed to set socket to non blocking for connection from ", address.ip, ":", address.port);
				Disconnect(id);
			}
		}
	}

	for (const PeerId peerId : _peerManager.GetAllPeerIds())
	{
		Peer peer = _peerManager.GetPeer(peerId);

		if (peer.socket < 0)
		{
			continue;
		}

		if (peer.state == ConnectionState::Connecting)
		{
            i32 err = 0;
            socklen_t len = sizeof(err);
            
            getsockopt(peer.socket, SOL_SOCKET, SO_ERROR, (char*)&err, &len);

            if (err == 0)
            {
                peer.state = ConnectionState::Connected;
                events.emplace(NetworkEventType::Connect, peer.id, 1, std::vector<u8>(), peer.address);

                _peerManager.UpdatePeer(peer);

                Log((u32)peer.state);
            }

            else
            {
                events.emplace(NetworkEventType::FailedConnection, peer.id, 1, std::vector<u8>(), peer.address);
                Disconnect(peer.id);

                continue;
            }
        }

        if (peer.state != ConnectionState::Connected)
        {
        	continue;
        }

        std::queue<std::vector<u8>>& sendQueue = _peerManager.GetSendQueue(peer.id);

    	while (!sendQueue.empty())
        {
            std::vector<u8>& buffer = sendQueue.front();
            if (!SendAll(peer.id, buffer))
            {
            	break;
            }

            sendQueue.pop();
        }

        while (true)
        {
        	u8 tmp[4096];

#ifdef _WIN32
            i64 r = recv(peer.socket, (char*)tmp, sizeof(tmp), 0);

            i32 err = WSAGetLastError();
        	if (r == SOCKET_ERROR && err == WSAEWOULDBLOCK)
        	{
        		break;
        	}
#else
            i64 r = recv(peer.socket, (char*)tmp, sizeof(tmp), MSG_DONTWAIT);

            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        	{
        		break;
        	}
#endif
            if (r < 0)
            {
            	events.emplace(NetworkEventType::Disconnect, peer.id, 1, std::vector<u8>(), peer.address);
                Disconnect(peer.id);

                break;
            }

            if (r == 0)
            {
                events.emplace(NetworkEventType::Disconnect, peer.id, 1, std::vector<u8>(), peer.address);
                Disconnect(peer.id);

                break;
            }

        	std::vector<u8>& recieveBuffer = _peerManager.GetRecieveBuffer(peer.id);

            recieveBuffer.insert(recieveBuffer.end(), tmp, tmp + r);

            while (recieveBuffer.size() >= sizeof(PacketHeader))
            {
                PacketHeader header;
                std::memcpy(&header, recieveBuffer.data(), sizeof(PacketHeader));

                if (recieveBuffer.size() < sizeof(PacketHeader) + header.size)
                {
                    break;
                }

                std::vector<u8> payload(recieveBuffer.begin() + sizeof(PacketHeader), recieveBuffer.begin() + sizeof(PacketHeader) + header.size);

                events.emplace(NetworkEventType::Receive, peer.id, 1, std::move(payload), peer.address);

                recieveBuffer.erase(recieveBuffer.begin(), recieveBuffer.begin() + sizeof(PacketHeader) + header.size);                    
            }
        }
	}

	if (minTimeMs)
	{
		auto endTime = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
		auto sleepTime = std::chrono::milliseconds(minTimeMs) - elapsed;

		if (sleepTime > std::chrono::milliseconds(0))
		{
			std::this_thread::sleep_for(sleepTime);
		}
	}
}

bool NetworkCoreTcp::Send(const PeerId peer, const std::vector<u8>& data) 
{
	const Peer p = GetPeer(peer);

	if (p.socket < 0)
	{
		LogColor(LOG_YELLOW, "Cannot send to peer ", peer, ", peer does not exist");
		return false;
	}

	if (p.state != ConnectionState::Connected)
	{
		LogColor(LOG_YELLOW, "Cannot send to a peer ", peer, ", peer isn't connected");
		return false;
	}

	PacketHeader header = {data.size()};
    std::vector<u8> packet(sizeof(header) + data.size());
    std::memcpy(packet.data(), &header, sizeof(header));

    if (!data.empty())
    {
    	std::memcpy(packet.data() + sizeof(header), data.data(), data.size());
    }

    if (!SendAll(peer, packet))
    {
        _peerManager.GetSendQueue(peer).push(std::move(packet));
    }

    return true;
}

Peer NetworkCoreTcp::GetPeer(const PeerId peer) 
{
	return _peerManager.GetPeer(peer);
}

bool NetworkCoreTcp::SendAll(const PeerId peer, const std::vector<u8>& data) 
{
	u32 size = data.size();
	const u8* dataPtr = data.data();
	const Peer p = GetPeer(peer);

	while(size > 0)
	{
#ifdef _WIN32
	    i32 sent = send(p.socket, (const char*)dataPtr, size, 0);
	    if (sent == SOCKET_ERROR)
	    {
	        return false;
	    }
#else
	    ssize_t sent = send(p.socket, dataPtr, size, 0);
	    if (sent < 0)
	    {
	        if (errno == EAGAIN || errno == EWOULDBLOCK)
	        {
	        	return false;
	        }

	        return false;
	    }
#endif
	    dataPtr += sent;
	    size -= sent;
	}

	return true;
}

bool NetworkCoreTcp::SetSocketNonBlocking(i32 socket) 
{
#ifdef _WIN32
    u64 mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    i32 flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0)
    {
    	return false;
    }

    flags = flags | O_NONBLOCK;
    return fcntl(socket, F_SETFL, flags) == 0;
#endif
}