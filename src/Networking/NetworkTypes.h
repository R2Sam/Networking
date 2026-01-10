#pragma once

#include "Types.h"

#include <string>
#include <vector>

using PeerId = u32;
using ChannelId = u8;

struct _ENetPeer;
using ws_cli_conn_t = u64;

struct Address
{
	std::string ip = "";
	u16 port = 0;
};

enum class ConnectionState
{
	Connecting,
	Connected,
	Disconnecting,
	Disconnected
};

struct Peer
{
	PeerId id = 0;
	_ENetPeer* enetPeer = nullptr;
	ws_cli_conn_t wsPeer = 0;
	Address address;
	ConnectionState state = ConnectionState::Disconnected;
};

enum class NetworkEventType
{
	Connect,
	FailedConnection,
	Disconnect,
	Receive
};

struct NetworkEvent
{
	NetworkEventType type;
	Peer peer;
	ChannelId channel;
	std::vector<u8> data;

	NetworkEvent(const NetworkEventType type, const Peer& peer, const ChannelId channel, const std::vector<u8>& data) :
	type(type),
	peer(peer),
	channel(channel),
	data(data)
	{

	}
};