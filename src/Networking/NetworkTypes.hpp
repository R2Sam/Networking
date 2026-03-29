#pragma once

#include "Types.hpp"

#include <string>
#include <vector>

using PeerId = u32;
using ChannelId = u8;

struct _ENetPeer; // NOLINT(readability-identifier-naming)
using ws_cli_conn_t = u64;

struct Address
{
	std::string ip;
	u16 port = 0;
};

enum class ConnectionState : u8
{
	CONNECTING,
	CONNECTED,
	DISCONNECTING,
	DISCONNECTED
};

struct Peer
{
	PeerId id = 0;
	_ENetPeer* enetPeer = nullptr;
	ws_cli_conn_t wsPeer = 0;
	Address address;
	u32 pingMs = 0;
	ConnectionState state = ConnectionState::DISCONNECTED;
};

enum class NetworkEventType : u8
{
	CONNECT,
	FAILED_CONNECTION,
	DISCONNECT,
	RECEIVE
};

struct NetworkEvent
{
	NetworkEventType type = NetworkEventType::CONNECT;
	Peer peer;
	ChannelId channel = 0;
	std::vector<std::byte> data;

	NetworkEvent(const NetworkEventType type, const Peer& peer, const ChannelId channel,
	const std::vector<std::byte>& data) :
	type(type),
	peer(peer),
	channel(channel),
	data(data)
	{
	}

	NetworkEvent()
	{
	}
};