#pragma once

#include "Types.hpp"

#include <queue>

namespace Networking
{
	struct Address
	{
		std::string ip;
		u16 port = {};
	};

	using PeerId = u64;
	using ChannelId = u8;

	enum ConnectionState : u8
	{
		CONNECTING,
		CONNECTED,
		DISCONNECTING,
		DISCONNECTED
	};

	struct Peer
	{
		PeerId id = {};
		Address address = {};
		u32 pingMs = {};
		ConnectionState state = DISCONNECTED;
	};

	enum EventType : u8
	{
		CONNECT,
		FAILED_CONNECTION,
		DISCONNECT,
		RECEIVE
	};

	struct Event
	{
		EventType type = {};

		Peer peer = {};
		ChannelId channelId = {};

		std::vector<std::byte> data;
	};

	bool InitServer(const u16 port, const u32 maxPeers = 64, const u32 channels = 1);
	bool InitClient(const u32 maxPeers = 64, const u32 channels = 1);

	void Close();

	std::optional<Networking::PeerId> Connect(const Address& address, const u32 data = 0);
	std::optional<Networking::PeerId> Connect(const std::string& ip, const u16 port, const u32 data = 0);
	void Disconnect(const PeerId peerId, const u32 data = 0);

	void Poll(std::queue<Event>& events, const u32 timeoutMs = 0);

	bool Send(const PeerId peerId, const std::byte* data, const u32 size, const ChannelId channel = 0,
	const bool reliable = true);
	bool Send(const PeerId peerId, const std::vector<std::byte>& data, const ChannelId channel = 0,
	const bool reliable = true);

	std::optional<Peer> GetPeer(const PeerId peerId);
	std::vector<PeerId> GetPeerIds();
}