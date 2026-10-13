#pragma once

#include "Networking/Node.hpp"

#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace Transport
{
	using NodeId = Node::NodeId;
	using PeerId = Networking::PeerId;
	using ChannelId = u8;

	enum class EventType : u8
	{
		CONNECTED,
		FAILED,
		DISCONNECTED,
		RECEIVE,
	};

	struct Event
	{
		EventType type = {};
		NodeId nodeId = {};
		ChannelId channelId = {};
		std::vector<std::byte> data;
	};

	struct SessionInfo
	{
		NodeId nodeId = {};
		std::string name;
		u32 rttMs = {};
		u32 cwnd = {};
		u32 ssthresh = {};
		u32 unacked = {};
		u32 pending = {};
	};

	bool Init(const u16 port, const std::string& name = "");
	void Shutdown();

	std::optional<PeerId> Connect(const Networking::Address& address);
	std::optional<PeerId> Connect(const std::string& ip, const u16 port);
	void Disconnect(const PeerId peerId);

	void Poll(std::queue<Event>& events, const u32 timeoutMs = 0);

	bool Send(const NodeId nodeId, const std::byte* data, const u32 size, const ChannelId channel = 0,
	const bool reliable = true);
	bool Send(const NodeId nodeId, const std::vector<std::byte>& data, const ChannelId channel = 0,
	const bool reliable = true);
	bool Send(const std::string& name, const std::byte* data, const u32 size, const ChannelId channel = 0,
	const bool reliable = true);
	bool Send(const std::string& name, const std::vector<std::byte>& data, const ChannelId channel = 0,
	const bool reliable = true);

	void Close(const NodeId nodeId);

	NodeId GetSelfId();
	std::vector<SessionInfo> GetSessions();
	std::vector<Node::Node> GetNodes();
}