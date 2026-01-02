#include "NetworkCore.h"

#define ENET_IMPLEMENTATION
#include "enet/enet.h"

#include "Log/Log.h"
#include "Assert.h"

#include <zlib.h>
#include <cstring>

NetworkCore::NetworkCore() 
{
	if (enet_initialize())
	{
		LogColor(LOG_YELLOW, "Failed to initalize enet");
	}
}

NetworkCore::~NetworkCore() 
{
	Shutdown();

	enet_deinitialize();
}

bool NetworkCore::InitServer(const u16 port, const u32 maxPeers, const u32 channels) 
{
	if (_host)
	{
		LogColor(LOG_RED, "Host already present");
		return false;
	}

	_ENetAddress addr;
	addr.host = ENET_HOST_ANY;
	addr.port = port;

	_host = enet_host_create(&addr, maxPeers, channels, 0, 0);

	if (!_host)
	{
		LogColor(LOG_RED, "Enet failed to create server at port: ", addr.port);
		return false;
	}

	AddCompression(_host);

	return true;
}

bool NetworkCore::InitClient(const u32 channels) 
{
	if (_host)
	{
		LogColor(LOG_RED, "Host already present");;
		return false;
	}

	_host = enet_host_create(nullptr, 1, channels, 0, 0);

	if (!_host)
	{
		LogColor(LOG_RED, "Enet failed to create client");
		return false;
	}

	AddCompression(_host);

	return true;
}

void NetworkCore::Shutdown() 
{
	if (_host)
	{
		enet_host_destroy(_host);
		_host = nullptr;
	}
}

PeerId NetworkCore::Connect(const Address& address, const u32 data)
{
	if (!_host) return 0;


	ENetAddress addr;
	enet_address_set_host(&addr, address.ip.c_str());
	addr.port = address.port;


	_ENetPeer* p = enet_host_connect(_host, &addr, _host->channelLimit, data);
	if (!p)
	{
		LogColor(LOG_YELLOW, "Failed to initialize connection to ", address.ip, ":", address.port);
		return 0;
	}

	PeerId id = _peerManager.AddPeer(p, address, ConnectionState::Connecting);

	return id;
}

void NetworkCore::Disconnect(const PeerId peer, const u32 data) 
{
	Peer p = _peerManager.GetPeer(peer);

	if (!p.enetPeer)
	{
		return;
	}

	p.state = ConnectionState::Disconnecting;

	enet_peer_disconnect(p.enetPeer, data);
}

void NetworkCore::Poll(std::queue<NetworkEvent>& events, const u32 timeoutMs)
{
	Assert(_host, "Cannot poll without a host");

	_ENetEvent event;

	while (enet_host_service(_host, &event, timeoutMs) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
			{
				HandleConnect(event, events);
			}
			break;

			case ENET_EVENT_TYPE_DISCONNECT:
			case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
			{
				HandleDisconnect(event, events);
			}
			break;

			case ENET_EVENT_TYPE_RECEIVE:
			{
				HandleReceive(event, events);
			}
			break;

			default:
			break;;
		}
	}
}

bool NetworkCore::Send(const PeerId peer, const std::vector<u8>& data, const ChannelId channel, const bool reliable) 
{
	Peer p = _peerManager.GetPeer(peer);

	if (!p.enetPeer)
	{
		LogColor(LOG_YELLOW, "Cannot sent to peer ", peer, ", peer does not exist");
		return false;
	}

	if (channel >= p.enetPeer->channelCount)
	{
		LogColor(LOG_YELLOW, "ChannelId was invalid when sending to peer ", peer);
		return false;
	}

	_ENetPacket* packet = enet_packet_create(data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	if (!packet)
	{
		LogColor(LOG_YELLOW, "Failed to create packet when sending to peer ", peer);
		return false;
	}

	u32 send = enet_peer_send(p.enetPeer, channel, packet);

	if (send)
	{
		enet_packet_destroy(packet);
		return false;
	}

	return true;
}

Peer NetworkCore::GetPeer(const PeerId peer) 
{
	return _peerManager.GetPeer(peer);
}

void NetworkCore::HandleConnect(const _ENetEvent& event, std::queue<NetworkEvent>& events) 
{
	Peer p = _peerManager.GetPeerEnet(event.peer->connectID);

	if (!p.enetPeer)
	{
		Address address;

		char hostBuf[256];
		enet_address_get_host_ip(&event.peer->address, hostBuf, sizeof(hostBuf));

		address.ip = hostBuf;
		address.port = event.peer->address.port;

		PeerId id = _peerManager.AddPeer(event.peer, address, ConnectionState::Connected);

		std::vector<u8> data(4);
		Assert(sizeof(event.data) == data.size(), "memcpy size must match");
		memcpy(data.data(), &event.data, data.size());

		events.emplace(NetworkEventType::Connect, id, 0, data, address);
	}

	else
	{
		events.emplace(NetworkEventType::Connect, p.id, 0, std::vector<u8>(), p.address);
	}
}

void NetworkCore::HandleDisconnect(const _ENetEvent& event, std::queue<NetworkEvent>& events) 
{
	Peer p = _peerManager.GetPeerEnet(event.peer->connectID);

	if (!p.enetPeer)
	{
		return;
	}

	if (p.state == ConnectionState::Connected)
	{
		events.emplace(NetworkEventType::Disconnect, p.id, 0, std::vector<u8>(), p.address);
	}

	else
	{
		events.emplace(NetworkEventType::FailedConnection, p.id, 0, std::vector<u8>(), p.address);
	}

	_peerManager.RemovePeer(p.id);
}

void NetworkCore::HandleReceive(const _ENetEvent& event, std::queue<NetworkEvent>& events) 
{
	Peer p = _peerManager.GetPeerEnet(event.peer->connectID);

	// Tough should we do something I don't think this is supposed to happen
	if (!p.enetPeer)
	{
		return;
	}

	std::vector<u8> data(event.packet->data, event.packet->data + event.packet->dataLength);
	enet_packet_destroy(event.packet);

	events.emplace(NetworkEventType::Receive, p.id, event.channelID, data, p.address);
}

void NetworkCore::AddCompression(_ENetHost* const _host) 
{
	ENetCompressor zlibCompressor = {0,
	[](void* context, const ENetBuffer* buffers, u64 bufferCount, u64 inputLimit, unsigned char* output, u64 outputLimit)
	{
		unsigned char* inputData = new unsigned char[inputLimit];
		u64 offset = 0;

		for (u64 i = 0; i < bufferCount; ++i)
		{
			memcpy(inputData + offset, buffers[i].data, buffers[i].dataLength);
			offset += buffers[i].dataLength;
		}

		u64 outputLength = outputLimit;
		u32 result = compress2(output, &outputLength, inputData, inputLimit, Z_BEST_SPEED);
		return u64(result == Z_OK ? outputLength : 0);
	},

	[](void* context, const unsigned char* input, u64 inputLength, unsigned char* output, u64 outputLength)
	{
		u32 result = uncompress(output, &outputLength, input, inputLength);
		return u64(result == Z_OK);
	},
	nullptr};

	enet_host_compress(_host, &zlibCompressor);
}