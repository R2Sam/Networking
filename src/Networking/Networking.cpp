#include "Networking.hpp"
#include "Assert.hpp"
#include "Log/Logger.hpp"
#include "Networking/Encryption.hpp"
#include "Timing.hpp"
#include "zlib/zlib.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <unordered_map>

#define ENET_IMPLEMENTATION
#include "enet/enet.h"
#undef ERROR

namespace
{
	struct InternalPeer
	{
		Networking::Peer peer = {};
		ENetPeer* enetPeer = nullptr;
	};

	ENetHost* s_host = nullptr;
	bool s_enetInit = false;

	std::unordered_map<Networking::PeerId, InternalPeer> s_peers;
	std::unordered_map<ENetPeer*, Networking::PeerId> s_enetPeers;

	Stopwatch s_pingStopwatch;

	bool Init()
	{
		if (s_host)
		{
			Logger::Write<LogLevel::ERROR>("Could not init, host already created");
			return false;
		}

		if (s_enetInit)
		{
			Logger::Write<LogLevel::WARN>("Tried to init enet twice");
			return true;
		}

		if (i32 error = enet_initialize(); error)
		{
			Logger::Write<LogLevel::ERROR>("Could not init enet with error: ", error);
			return false;
		}

		s_enetInit = true;

		if (!Encryption::InitEncryption())
		{
			Logger::Write<LogLevel::ERROR>("Failed to init encryption");
			return false;
		}

		return true;
	}

	Networking::PeerId ConvertPeerId(const ENetPeer* peer)
	{
		return (static_cast<u64>(peer->outgoingPeerID) << 32) | static_cast<u64>(peer->connectID);
	}

	void HandleConnect(const ENetEvent& event, std::queue<Networking::Event>& events)
	{
		Networking::PeerId id = {};

		auto it = s_enetPeers.find(event.peer);
		if (it == s_enetPeers.end())
		{
			id = ConvertPeerId(event.peer);
			Networking::Address address = {};

			char hostBuf[256] = {};
			if (i32 error = enet_address_get_host_ip(&event.peer->address, hostBuf, sizeof(hostBuf)); error != 0)
			{
				Logger::Write<LogLevel::WARN>("Failed to get host IP for connected peer: ", id, " with error: ", error);
				address.ip = "unknown";
			}

			else
			{
				address.ip = hostBuf;
			}

			address.port = event.peer->address.port;

			s_peers.emplace(id, InternalPeer{{id, address, 0, Networking::CONNECTED}, event.peer});
			s_enetPeers.emplace(event.peer, id);
		}

		else
		{
			id = it->second;

			Assert(s_peers.contains(id));

			s_peers[id].peer.state = Networking::CONNECTED;
		}

		std::vector<std::byte> data(4);
		Assert(data.size() == sizeof(event.data));
		memcpy(data.data(), &event.data, data.size());

		events.emplace(Networking::CONNECT, s_peers[id].peer, event.channelID, data);
	}

	void HandleDisconnect(const ENetEvent& event, std::queue<Networking::Event>& events)
	{
		Assert(s_enetPeers.contains(event.peer));

		Networking::PeerId id = s_enetPeers[event.peer];

		std::vector<std::byte> data(4);
		Assert(data.size() == sizeof(event.data));
		memcpy(data.data(), &event.data, data.size());

		Networking::ConnectionState state = s_peers[id].peer.state;

		events.emplace(state == Networking::CONNECTED || state == Networking::DISCONNECTING
					   ? Networking::DISCONNECT
					   : Networking::FAILED_CONNECTION,
		s_peers[id].peer, event.channelID, data);

		s_peers.erase(id);
		s_enetPeers.erase(event.peer);
	}

	void HandleReceive(const ENetEvent& event, std::queue<Networking::Event>& events)
	{
		Assert(s_enetPeers.contains(event.peer));

		Networking::PeerId id = s_enetPeers[event.peer];

		std::byte* ptr = reinterpret_cast<std::byte*>(event.packet->data);

		std::vector<std::byte> data = Encryption::Decrypt(ptr, event.packet->dataLength);
		enet_packet_destroy(event.packet);

		events.emplace(Networking::RECEIVE, s_peers[id].peer, event.channelID, std::move(data));
	}

	size_t Compress([[maybe_unused]] void* context, const ENetBuffer* buffers, size_t bufferCount, size_t inputLimit,
	unsigned char* output, size_t outputLimit)
	{
		std::vector<std::byte> inputData(inputLimit);

		size_t offset = 0;

		for (size_t i = 0; i < bufferCount; ++i)
		{
			if (offset + buffers[i].dataLength > inputLimit)
			{
				return 0;
			}

			memcpy(inputData.data() + offset, buffers[i].data, buffers[i].dataLength);
			offset += buffers[i].dataLength;
		}

		uLongf outputLength = outputLimit;
		size_t result = compress2(output, &outputLength, reinterpret_cast<u8*>(inputData.data()), offset, Z_BEST_SPEED);
		return size_t(result == Z_OK ? outputLength : 0);
	}

	size_t Decompress([[maybe_unused]] void* context, const unsigned char* input, size_t inputLength,
	unsigned char* output, size_t outputLength)
	{
		uLongf outputLengthULongf = outputLength;
		u32 result = uncompress(output, &outputLengthULongf, input, inputLength);

		return (result == Z_OK ? outputLengthULongf : 0);
	}
}

bool Networking::InitServer(const u16 port, const u32 maxPeers, const u32 channels)
{
	if (s_host)
	{
		Logger::Write<LogLevel::ERROR>("Could not init server, host already created");
		return false;
	}

	Assert(port < 65000 && maxPeers < 1000 && maxPeers > 0 && channels < 255,
	"Port, maxPeers and channels must be in ranges");

	if (!Init())
	{
		return false;
	}

	ENetAddress addr = {};
	addr.host = ENET_HOST_ANY;
	addr.port = port;

	s_host = enet_host_create(&addr, maxPeers, channels, 0, 0);

	if (!s_host)
	{
		Logger::Write<LogLevel::ERROR>("Failed to create server at port: ", addr.port);
		return false;
	}

	ENetCompressor zlibCompressor = {0, &Compress, &Decompress, nullptr};
	enet_host_compress(s_host, &zlibCompressor);

	return true;
}

bool Networking::InitClient(const u32 maxPeers, const u32 channels)
{
	if (s_host)
	{
		Logger::Write<LogLevel::ERROR>("Could not init client, host already created");
		return false;
	}

	Assert(maxPeers < 1000 && maxPeers > 0 && channels < 255, "MaxPeers and channels must be in ranges");

	if (!Init())
	{
		return false;
	}

	s_host = enet_host_create(nullptr, maxPeers, channels, 0, 0);

	if (!s_host)
	{
		Logger::Write<LogLevel::ERROR>("Failed to create client");
		return false;
	}

	ENetCompressor zlibCompressor = {0, &Compress, &Decompress, nullptr};
	enet_host_compress(s_host, &zlibCompressor);

	return true;
}

void Networking::Close()
{
	if (s_enetInit)
	{
		s_enetInit = false;

		enet_deinitialize();
	}

	if (s_host)
	{
		enet_host_destroy(s_host);

		s_host = nullptr;
	}

	s_peers.clear();
	s_enetPeers.clear();
}

std::optional<Networking::PeerId> Networking::Connect(const Address& address, const u32 data)
{
	if (!s_host)
	{
		Logger::Write<LogLevel::WARN>("Can not connect without a host to: ", address.ip, ":", address.port);
		return std::nullopt;
	}

	ENetAddress addr = {};
	if (i32 error = enet_address_set_host(&addr, address.ip.c_str()); error)
	{
		Logger::Write<LogLevel::WARN>("Failed to resolve ip: ", address.ip, ":", address.port, " with error: ", error);
		return std::nullopt;
	}

	addr.port = address.port;

	ENetPeer* peer = enet_host_connect(s_host, &addr, s_host->channelLimit, data);
	if (!peer)
	{
		Logger::Write<LogLevel::WARN>("Failed to init connection to: ", address.ip, ":", address.port);
		return std::nullopt;
	}

	PeerId id = ConvertPeerId(peer);

	s_peers.emplace(id, InternalPeer{{id, address, 0, Networking::CONNECTING}, peer});
	s_enetPeers.emplace(peer, id);

	return id;
}

std::optional<Networking::PeerId> Networking::Connect(const std::string& ip, const u16 port, const u32 data)
{
	return Connect({ip, port}, data);
}

void Networking::Disconnect(const PeerId peerId, const u32 data)
{
	auto it = s_peers.find(peerId);
	if (it == s_peers.end())
	{
		return;
	}

	InternalPeer& internalPeer = it->second;

	if (internalPeer.peer.state != Networking::CONNECTED)
	{
		Logger::Write<LogLevel::WARN>("Tried to disconnect peer: ", peerId, " that is not connected");
		return;
	}

	internalPeer.peer.state = Networking::DISCONNECTING;

	enet_peer_disconnect(internalPeer.enetPeer, data);
}

void Networking::Poll(std::queue<Event>& events, const u32 timeoutMs)
{
	Assert(s_host, "Cannot poll without a host");

	u32 timeout = timeoutMs;
	ENetEvent event = {};

	while (i32 result = enet_host_service(s_host, &event, timeout))
	{
		if (result < 0)
		{
			Logger::Write<LogLevel::WARN>("Error when polling: ", result);
			break;
		}

		timeout = 0;

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

	double lastPingMs = s_pingStopwatch.Check();
	if (lastPingMs == 0 || lastPingMs >= 500)
	{
		for (auto& [peerId, internalPeer] : s_peers)
		{
			if (internalPeer.peer.state != ConnectionState::CONNECTED || internalPeer.enetPeer == nullptr)
			{
				continue;
			}

			internalPeer.peer.pingMs = internalPeer.enetPeer->roundTripTime;
		}

		s_pingStopwatch.Stop();
		s_pingStopwatch.Start();
	}
}

bool Networking::Send(const PeerId peerId, const std::byte* data, const u32 size, const ChannelId channel,
const bool reliable)
{
	auto it = s_peers.find(peerId);
	if (it == s_peers.end() || !it->second.enetPeer)
	{
		Logger::Write<LogLevel::WARN>("Failed to send packet to non existent peer: ", peerId);
		return false;
	}

	InternalPeer& internalPeer = it->second;

	Assert(channel <= internalPeer.enetPeer->channelCount - 1, "Channel id must be in range");

	std::vector<std::byte> encryptedData = Encryption::Encrypt(data, size);

	ENetPacket* packet =
	enet_packet_create(encryptedData.data(), encryptedData.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
	if (!packet)
	{
		Logger::Write<LogLevel::WARN>("Failed to create packet when sending to peer: ", peerId);
		return false;
	}

	if (i32 error = enet_peer_send(internalPeer.enetPeer, channel, packet); error < 0)
	{
		enet_packet_destroy(packet);

		Logger::Write<LogLevel::WARN>("Failed to send packet to peer: ", peerId, " with error: ", error);
		return false;
	}

	return true;
}

bool Networking::Send(const PeerId peerId, const std::vector<std::byte>& data, const ChannelId channel,
const bool reliable)
{
	return Send(peerId, data.data(), data.size(), channel, reliable);
}

std::optional<Networking::Peer> Networking::GetPeer(const PeerId peerId)
{
	auto it = s_peers.find(peerId);
	if (it == s_peers.end())
	{
		return std::nullopt;
	}

	return it->second.peer;
}

std::vector<Networking::PeerId> Networking::GetPeerIds()
{
	std::vector<PeerId> peers;
	peers.reserve(s_peers.size());

	for (const auto& [id, peer] : s_peers)
	{
		peers.emplace_back(id);
	}

	return peers;
}