#define ENET_IMPLEMENTATION
#include "enet/enet.h"

#include "NetworkCore.h"

#include "zlib/zlib.h"

#include "Assert.h"
#include "Log/Log.h"

#include <atomic>
#include <cstring>

#ifdef TESTS
#include "catch2/catch_test_macros.hpp"

TEST_CASE("Test test", "[init]")
{
	REQUIRE(true);
}

#endif

static std::atomic<u32> s_instances = 0;

NetworkCore::NetworkCore()
{
	if (!s_instances)
	{
		if (enet_initialize())
		{
			LogColor(LOG_YELLOW, "Failed to initialize enet");
		}
	}

	++s_instances;
}

NetworkCore::~NetworkCore()
{
	Shutdown();

	--s_instances;

	if (!s_instances)
	{
		enet_deinitialize();
	}
}

bool NetworkCore::InitServer(const u16 port, const u32 maxPeers, const u32 channels)
{
	if (m_host)
	{
		LogColor(LOG_RED, "Host already present");
		return false;
	}

	Assert(port < 65000 && maxPeers < 4000 && channels < 255, "Port, maxPeers and channels must be in ranges");

	_ENetAddress addr;
	addr.host = ENET_HOST_ANY;
	addr.port = port;

	m_host = enet_host_create(&addr, maxPeers, channels, 0, 0);

	if (!m_host)
	{
		LogColor(LOG_RED, "Enet failed to create server at port: ", addr.port);
		return false;
	}

	AddCompression(m_host);

	return true;
}

bool NetworkCore::InitClient(const u32 maxPeers, const u32 channels)
{
	if (m_host)
	{
		LogColor(LOG_RED, "Host already present");
		return false;
	}

	Assert(maxPeers < 4000 && channels < 255, "MaxPeers and channels must be in ranges");

	m_host = enet_host_create(nullptr, maxPeers, channels, 0, 0);

	if (!m_host)
	{
		LogColor(LOG_RED, "Enet failed to create client");
		return false;
	}

	AddCompression(m_host);

	return true;
}

void NetworkCore::Shutdown()
{
	if (m_host)
	{
		enet_host_destroy(m_host);
		m_host = nullptr;
	}
}

Peer NetworkCore::Connect(const Address& address, const u32 data)
{
	if (!m_host)
	{
		return {};
	}

	ENetAddress addr;
	if (enet_address_set_host(&addr, address.ip.c_str()) < 0)
	{
		LogColor(LOG_YELLOW, "Failed to resolve ip ", address.ip);
		return {};
	}
	addr.port = address.port;

	_ENetPeer* p = enet_host_connect(m_host, &addr, m_host->channelLimit, data);
	if (!p)
	{
		LogColor(LOG_YELLOW, "Failed to initialize connection to ", address.ip, ":", address.port);
		return {};
	}

	Peer peer = m_peerManager.AddPeer(p, address, ConnectionState::CONNECTING);

	return peer;
}

void NetworkCore::Disconnect(const PeerId peerId, const u32 data)
{
	Peer peer = m_peerManager.GetPeer(peerId);

	if (!peer.enetPeer)
	{
		return;
	}

	peer.state = ConnectionState::DISCONNECTING;

	enet_peer_disconnect(peer.enetPeer, data);
}

void NetworkCore::Poll(std::queue<NetworkEvent>& events, const u32 timeoutMs)
{
	Assert(m_host, "Cannot poll without a host");

	_ENetEvent event;

	while (enet_host_service(m_host, &event, timeoutMs) > 0)
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
			break;
		}
	}

	for (u32 i = 0; i < m_host->peerCount; ++i)
	{
		ENetPeer enetPeer = m_host->peers[i];

		Peer peer = m_peerManager.GetPeerEnet(enetPeer.connectID);
		peer.pingMs = enetPeer.roundTripTime;
		m_peerManager.EditPeer(peer);
	}
}

bool NetworkCore::Send(const PeerId peerId, std::vector<u8>&& data, const ChannelId channel, const bool reliable)
{
	Peer peer = m_peerManager.GetPeer(peerId);

	if (!peer.enetPeer)
	{
		LogColor(LOG_YELLOW, "Cannot sent to peer ", peerId, ", peer does not exist");
		return false;
	}

	if (channel >= peer.enetPeer->channelCount)
	{
		LogColor(LOG_YELLOW, "ChannelId was invalid when sending to peer ", peerId);
		return false;
	}

	_ENetPacket* packet = enet_packet_create(data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	if (!packet)
	{
		LogColor(LOG_YELLOW, "Failed to create packet when sending to peer ", peerId);
		return false;
	}

	u32 send = enet_peer_send(peer.enetPeer, channel, packet);

	if (send)
	{
		enet_packet_destroy(packet);
		return false;
	}

	return true;
}

bool NetworkCore::Send(const PeerId peerId, const u8* data, const u32 size, const ChannelId channel,
const bool reliable)
{
	Peer peer = m_peerManager.GetPeer(peerId);

	if (!peer.enetPeer)
	{
		LogColor(LOG_YELLOW, "Cannot sent to peer ", peerId, ", peer does not exist");
		return false;
	}

	if (channel >= peer.enetPeer->channelCount)
	{
		LogColor(LOG_YELLOW, "ChannelId was invalid when sending to peer ", peerId);
		return false;
	}

	_ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	if (!packet)
	{
		LogColor(LOG_YELLOW, "Failed to create packet when sending to peer ", peerId);
		return false;
	}

	u32 send = enet_peer_send(peer.enetPeer, channel, packet);

	if (send)
	{
		enet_packet_destroy(packet);
		return false;
	}

	return true;
}

Peer NetworkCore::GetPeer(const PeerId peerId) const
{
	return m_peerManager.GetPeer(peerId);
}

std::unordered_map<PeerId, Peer> NetworkCore::GetPeers() const
{
	return m_peerManager.GetPeers();
}

void NetworkCore::HandleConnect(const _ENetEvent& event, std::queue<NetworkEvent>& events)
{
	Peer peer = m_peerManager.GetPeerEnet(event.peer->connectID);

	if (!peer.enetPeer)
	{
		Address address;

		char hostBuf[256];
		enet_address_get_host_ip(&event.peer->address, hostBuf, sizeof(hostBuf));

		address.ip = hostBuf;
		address.port = event.peer->address.port;

		Peer newPeer = m_peerManager.AddPeer(event.peer, address, ConnectionState::CONNECTED);

		std::vector<u8> data(4);
		Assert(sizeof(event.data) == data.size(), "memcpy size must match");
		memcpy(data.data(), &event.data, data.size());

		events.emplace(NetworkEventType::CONNECT, newPeer, 0, data);
	}

	else
	{
		peer.state = ConnectionState::CONNECTED;
		m_peerManager.EditPeer(peer);
		events.emplace(NetworkEventType::CONNECT, peer, 0, std::vector<u8>());
	}
}

void NetworkCore::HandleDisconnect(const _ENetEvent& event, std::queue<NetworkEvent>& events)
{
	Peer peer = m_peerManager.GetPeerEnet(event.peer->connectID);

	if (!peer.enetPeer)
	{
		return;
	}

	if (peer.state == ConnectionState::CONNECTING)
	{
		events.emplace(NetworkEventType::FAILED_CONNECTION, peer, 0, std::vector<u8>());
	}

	else
	{
		events.emplace(NetworkEventType::DISCONNECT, peer, 0, std::vector<u8>());
	}

	m_peerManager.RemovePeer(peer.id);
}

void NetworkCore::HandleReceive(const _ENetEvent& event, std::queue<NetworkEvent>& events)
{
	Peer peer = m_peerManager.GetPeerEnet(event.peer->connectID);

	// Tough should we do something I don't think this is supposed to happen
	if (!peer.enetPeer)
	{
		return;
	}

	std::vector<u8> data(event.packet->data, event.packet->data + event.packet->dataLength);
	enet_packet_destroy(event.packet);

	events.emplace(NetworkEventType::RECEIVE, peer, event.channelID, data);
}

static size_t Compress(void* context, const ENetBuffer* buffers, size_t bufferCount, size_t inputLimit,
unsigned char* output, size_t outputLimit);
static size_t Decompress(void* context, const unsigned char* input, size_t inputLength, unsigned char* output,
size_t outputLength);

void NetworkCore::AddCompression(_ENetHost* const host)
{
	ENetCompressor zlibCompressor = {0, &Compress, &Decompress, nullptr};

	enet_host_compress(host, &zlibCompressor);
}

size_t Compress(void* context, const ENetBuffer* buffers, size_t bufferCount, size_t inputLimit, unsigned char* output,
size_t outputLimit)
{
	std::vector<u8> inputData(inputLimit);

	size_t offset = 0;

	for (size_t i = 0; i < bufferCount; ++i)
	{
		memcpy(inputData.data() + offset, buffers[i].data, buffers[i].dataLength);
		offset += buffers[i].dataLength;
	}

	uLongf outputLength = outputLimit;
	size_t result = compress2(output, &outputLength, inputData.data(), inputLimit, Z_BEST_SPEED);
	return size_t(result == Z_OK ? outputLength : 0);
}

size_t Decompress(void* context, const unsigned char* input, size_t inputLength, unsigned char* output,
size_t outputLength)
{
	uLongf outputLengthULongf = outputLength;
	u32 result = uncompress(output, &outputLengthULongf, input, inputLength);
	return size_t(result == Z_OK);
}