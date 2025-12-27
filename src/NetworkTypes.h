#pragma once

#include "Types.h"

#include <string>
#include <vector>

using PeerId = u32;
using ChannelId = u8;

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
	PeerId peer;
	ChannelId channel;
	std::vector<u8> data;
	Address address;

	NetworkEvent(const NetworkEventType type, const PeerId peer, const ChannelId channel, const std::vector<u8>& data, const Address& address) :
	type(type),
	peer(peer),
	channel(channel),
	data(data),
	address(address)
	{

	}
};