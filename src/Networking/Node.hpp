#pragma once

#include "Networking/Networking.hpp"

namespace Node
{
	using NodeId = u64;

	struct Node
	{
		NodeId id = {};
		std::string name;
	};

	bool Init(const u16 port, const std::string& name = "");
	void Shutdown();

	std::optional<Networking::PeerId> Connect(const Networking::Address& address);
	std::optional<Networking::PeerId> Connect(const std::string& ip, const u16 port);
	void Disconnect(const Networking::PeerId peerId);

	void Poll(std::queue<Networking::Event>& events, const u32 timeoutMs);

	bool Send(const NodeId nodeId, const std::byte* data, const u32 size, const bool reliable = true);
	bool Send(const NodeId nodeId, const std::vector<std::byte>& data, const bool reliable = true);

	bool Send(const std::string& name, const std::byte* data, const u32 size, const bool reliable = true);
	bool Send(const std::string& name, const std::vector<std::byte>& data, const bool reliable = true);

	std::vector<std::pair<Node, Networking::PeerId>> GetRoutes();
}