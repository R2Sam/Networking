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
};