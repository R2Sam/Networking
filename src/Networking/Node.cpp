// Node.cpp
#include "Networking/Node.hpp"
#include "Log/Logger.hpp"
#include "cereal/MyCereal.h"
#include "libsodium/sodium/randombytes.h"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <vector>

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

namespace
{
	using NodeId = Node::NodeId;
	using PeerId = Networking::PeerId;
	using ConnectionState = Networking::ConnectionState;

	constexpr u32 ANNOUNCE_INTERVAL_MS = 1000;
	constexpr u32 ANNOUNCE_TTL = 8;
	constexpr u32 DATA_TTL = 16;
	constexpr u32 ROUTE_TIMEOUT_MS = ANNOUNCE_INTERVAL_MS * 3;
	constexpr u32 MAX_PENDING_PER_DEST = 32;

	enum class PacketType : u8
	{
		ANNOUNCE = 1,
		DATA = 2,
		DEPARTURE = 3,
	};

	struct Announce
	{
		NodeId nodeId = 0;
		std::string name;
		u32 seq = 0;
		u32 ttl = 0;

		template <class Archive>
		void serialize(Archive& ar) // NOLINT
		{
			ar(nodeId, name, seq, ttl);
		}
	};

	struct Data
	{
		NodeId dst = 0;
		NodeId src = 0;
		u32 ttl = 0;
		std::vector<u8> payload;

		template <class Archive>
		void serialize(Archive& ar) // NOLINT
		{
			ar(dst, src, ttl, payload);
		}
	};

	struct Departure
	{
		NodeId node = 0;
		u32 ttl = ANNOUNCE_TTL;

		template <class Archive>
		void serialize(Archive& ar) // NOLINT
		{
			ar(node, ttl);
		}
	};

	struct RouteEntry
	{
		NodeId nodeId = 0;
		std::string name;
		PeerId nextHop = 0;
		u32 seq = 0;
		u32 hops = 0;
		std::chrono::steady_clock::time_point lastSeen;
	};

	NodeId s_selfId = 0;
	std::string s_selfName;
	u32 s_selfSeq = 0;

	std::unordered_map<NodeId, RouteEntry> s_routes;
	std::unordered_map<NodeId, u32> s_seenSeq;
	std::unordered_map<PeerId, NodeId> s_peerToNode;
	std::unordered_map<NodeId, std::queue<std::vector<std::byte>>> s_pending;

	std::chrono::steady_clock::time_point s_lastAnnounce{};

	std::vector<std::byte> PackMessage(const PacketType type, const std::vector<std::byte>& body)
	{
		std::vector<std::byte> data;
		data.reserve(body.size() + 1);

		data.push_back(static_cast<std::byte>(type));
		data.insert(data.end(), body.begin(), body.end());

		return data;
	}

	void SendRaw(const PeerId peer, const std::vector<std::byte>& data, const bool reliable)
	{
		if (!Networking::Send(peer, data, 0, reliable))
		{
			Logger::Write<LogLevel::DEBUG>("Node failed to send raw packet to peer: ", peer);
		}
	}

	void Broadcast(const std::vector<std::byte>& data, const bool reliable, const PeerId except = 0)
	{
		for (const PeerId peerId : Networking::GetPeerIds())
		{
			if (peerId == except)
			{
				continue;
			}

			const auto peer = Networking::GetPeer(peerId);
			if (!peer || peer->state != ConnectionState::CONNECTED)
			{
				continue;
			}

			SendRaw(peerId, data, reliable);
		}
	}

	void UpdateSelfRoute()
	{
		RouteEntry self;
		self.nodeId = s_selfId;
		self.name = s_selfName;
		self.nextHop = 0;
		self.seq = s_selfSeq;
		self.hops = 0;
		self.lastSeen = std::chrono::steady_clock::now();
		s_routes[s_selfId] = self;
	}

	void SendOwnAnnuounce(const PeerId onlyTo = 0)
	{
		Announce announce;
		announce.nodeId = s_selfId;
		announce.name = s_selfName;
		announce.seq = ++s_selfSeq;
		announce.ttl = ANNOUNCE_TTL;

		const auto msg = PackMessage(PacketType::ANNOUNCE, Serialize(announce));

		if (onlyTo != 0)
		{
			Logger::Write<LogLevel::TRACE>("Node sending announce to peer: ", onlyTo, " seq: ", announce.seq);
			SendRaw(onlyTo, msg, false);
		}

		else
		{
			Logger::Write<LogLevel::TRACE>("Node broadcasting announce seq: ", announce.seq);
			Broadcast(msg, false);
		}

		UpdateSelfRoute();
	}

	void ForwardAnnounce(const Announce& annouce, const PeerId from)
	{
		Logger::Write<LogLevel::TRACE>("Node forwarding announce for node: ", annouce.nodeId, " seq: ", annouce.seq,
		" ttl: ", annouce.ttl, " except peer: ", from);
		const auto msg = PackMessage(PacketType::ANNOUNCE, Serialize(annouce));
		Broadcast(msg, false, from);
	}

	void FlushPending(const NodeId dst, const PeerId nextHop)
	{
		auto it = s_pending.find(dst);
		if (it == s_pending.end())
		{
			return;
		}

		auto& q = it->second;
		const size_t count = q.size();
		Logger::Write<LogLevel::DEBUG>("Node flushing ", count, " pending packets to node: ", dst,
		" via peer: ", nextHop);
		while (!q.empty())
		{
			Data data;
			data.dst = dst;
			data.src = s_selfId;
			data.ttl = DATA_TTL;
			data.payload.resize(q.front().size());

			if (!q.front().empty())
			{
				std::memcpy(data.payload.data(), q.front().data(), q.front().size());
			}
			q.pop();

			const auto msg = PackMessage(PacketType::DATA, Serialize(data));
			SendRaw(nextHop, msg, true);
		}

		s_pending.erase(it);
	}

	void HandleAnnounce(const Announce& announce, const PeerId from)
	{
		if (announce.nodeId == s_selfId)
		{
			Logger::Write<LogLevel::DEBUG>("Node received its own announce back from peer: ", from);
			return;
		}

		const u32 hops = (announce.ttl <= ANNOUNCE_TTL) ? (ANNOUNCE_TTL - announce.ttl + 1) : 1;

		auto it = s_routes.find(announce.nodeId);
		bool update = false;

		if (it == s_routes.end())
		{
			update = true;
		}

		else if (announce.seq > it->second.seq)
		{
			update = true;
		}

		else if (announce.seq == it->second.seq && hops < it->second.hops)
		{
			update = true;
		}

		if (update)
		{
			Logger::Write<LogLevel::DEBUG>("Node added/updated route to node: ", announce.nodeId,
			" name: ", announce.name, " via peer: ", from, " seq: ", announce.seq, " hops: ", hops);

			RouteEntry entry;
			entry.nodeId = announce.nodeId;
			entry.name = announce.name;
			entry.nextHop = from;
			entry.seq = announce.seq;
			entry.hops = hops;
			entry.lastSeen = std::chrono::steady_clock::now();
			s_routes[announce.nodeId] = entry;

			FlushPending(announce.nodeId, from);
		}

		else if (it != s_routes.end())
		{
			it->second.lastSeen = std::chrono::steady_clock::now();

			if (!announce.name.empty() && it->second.name != announce.name)
			{
				Logger::Write<LogLevel::DEBUG>("Node updated name for node: ", announce.nodeId,
				" from: ", it->second.name, " to: ", announce.name);
				it->second.name = announce.name;
			}
		}

		if (hops == 1)
		{
			s_peerToNode[from] = announce.nodeId;
		}

		auto sit = s_seenSeq.find(announce.nodeId);
		if (sit == s_seenSeq.end() || announce.seq > sit->second)
		{
			s_seenSeq[announce.nodeId] = announce.seq;

			if (announce.ttl > 1)
			{
				Announce fwd = announce;
				fwd.ttl = announce.ttl - 1;
				ForwardAnnounce(fwd, from);
			}
		}
	}

	void HandleData(const Data& data, const PeerId from, std::queue<Networking::Event>& out)
	{
		if (data.dst == s_selfId)
		{
			Networking::Event ev;
			ev.type = Networking::EventType::RECEIVE;

			if (const auto peer = Networking::GetPeer(from))
			{
				ev.peer = *peer;
			}

			ev.channelId = 0;
			ev.data.resize(data.payload.size());

			if (!data.payload.empty())
			{
				std::memcpy(ev.data.data(), data.payload.data(), data.payload.size());
			}

			out.push(std::move(ev));
			return;
		}

		if (data.ttl == 0)
		{
			Logger::Write<LogLevel::WARN>("Node dropping data for node: ", data.dst, " from node: ", data.src,
			" ttl exhausted");
			return;
		}

		auto it = s_routes.find(data.dst);
		if (it == s_routes.end())
		{
			Logger::Write<LogLevel::DEBUG>("Node dropping data for node: ", data.dst, " from node: ", data.src,
			" no route");
			return;
		}

		if (it->second.nextHop == from)
		{
			Logger::Write<LogLevel::WARN>("Node dropping data for node: ", data.dst, " from node: ", data.src,
			" would loop back to peer: ", from);
			return;
		}

		Logger::Write<LogLevel::TRACE>("Node forwarding data for node: ", data.dst, " from node: ", data.src,
		" via peer: ", it->second.nextHop, " ttl: ", data.ttl);

		Data fwd = data;
		fwd.ttl = data.ttl - 1;
		const auto msg = PackMessage(PacketType::DATA, Serialize(fwd));
		SendRaw(it->second.nextHop, msg, true);
	}

	void HandleDeparture(const Departure& departure, const PeerId from)
	{
		if (departure.node == s_selfId)
		{
			Logger::Write<LogLevel::DEBUG>("Node received its own departure back from peer: ", from);
			return;
		}

		bool removed = false;

		auto it = s_routes.find(departure.node);
		if (it != s_routes.end() && it->second.nextHop == from)
		{
			Logger::Write<LogLevel::DEBUG>("Node removed route to node: ", departure.node,
			" after departure from peer: ", from);
			s_routes.erase(it);
			s_pending.erase(departure.node);
			removed = true;
		}

		if (removed && departure.ttl > 1)
		{
			Departure fwd = departure;
			fwd.ttl = departure.ttl - 1;
			const auto msg = PackMessage(PacketType::DEPARTURE, Serialize(fwd));
			Broadcast(msg, false, from);
		}
	}

	void RemoveRoutesVia(const PeerId peer)
	{
		s_peerToNode.erase(peer);

		for (auto it = s_routes.begin(); it != s_routes.end();)
		{
			if (it->second.nodeId != s_selfId && it->second.nextHop == peer)
			{
				Logger::Write<LogLevel::DEBUG>("Node removed route to node: ", it->second.nodeId, " via peer: ", peer);
				s_pending.erase(it->first);
				it = s_routes.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void ExpireRoutes()
	{
		const auto now = std::chrono::steady_clock::now();

		for (auto it = s_routes.begin(); it != s_routes.end();)
		{
			if (it->second.nodeId == s_selfId)
			{
				++it;
				continue;
			}

			const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.lastSeen).count();

			if (ageMs > ROUTE_TIMEOUT_MS)
			{
				Logger::Write<LogLevel::INFO>("Node expired route to node: ", it->second.nodeId, " after ", ageMs,
				" ms via peer: ", it->second.nextHop);
				s_pending.erase(it->first);
				it = s_routes.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}

bool Node::Init(const u16 port, const std::string& name)
{
	const bool ok = (port == 0) ? Networking::InitClient() : Networking::InitServer(port);
	if (!ok)
	{
		Logger::Write<LogLevel::ERROR>("Node failed to init networking on port: ", port);
		return false;
	}

	u8 buffer[sizeof(s_selfId)] = {};
	do
	{
		randombytes(buffer, sizeof(buffer));
		memcpy(&s_selfId, buffer, sizeof(s_selfId));
	} while (s_selfId == 0);

	s_selfName = name;
	s_selfSeq = 0;

	s_routes.clear();
	s_seenSeq.clear();
	s_peerToNode.clear();
	s_pending.clear();

	UpdateSelfRoute();

	Logger::Write<LogLevel::INFO>("Node initialized with id: ", s_selfId, " name: ", s_selfName,
	" port: ", Networking::GetLocalPort());

	s_lastAnnounce = std::chrono::steady_clock::now() - std::chrono::milliseconds(ANNOUNCE_INTERVAL_MS);
	return true;
}

void Node::Shutdown()
{
	Logger::Write<LogLevel::INFO>("Node shutting down id: ", s_selfId, " name: ", s_selfName);

	s_routes.clear();
	s_seenSeq.clear();
	s_peerToNode.clear();
	s_pending.clear();
	s_selfId = 0;
	s_selfSeq = 0;
	s_selfName.clear();

	Networking::Shutdown();
}

std::optional<PeerId> Node::Connect(const Networking::Address& address)
{
	const auto peerId = Networking::Connect(address);
	if (!peerId)
	{
		Logger::Write<LogLevel::WARN>("Node failed to connect to: ", address.ip, ":", address.port);
	}

	return peerId;
}

std::optional<PeerId> Node::Connect(const std::string& ip, const u16 port)
{
	return Connect({ip, port});
}

void Node::Disconnect(const PeerId peerId)
{
	Logger::Write<LogLevel::DEBUG>("Node disconnecting peer: ", peerId);
	Networking::Disconnect(peerId);
}

void Node::Poll(std::queue<Networking::Event>& events, const u32 timeoutMs)
{
	std::queue<Networking::Event> rawEvents;
	Networking::Poll(rawEvents, timeoutMs);

	while (!rawEvents.empty())
	{
		auto& event = rawEvents.front();

		switch (event.type)
		{
		case Networking::EventType::CONNECT:
			Logger::Write<LogLevel::DEBUG>("Node handling connect for peer: ", event.peer.id);
			SendOwnAnnuounce(event.peer.id);
			events.push(event);
			break;

		case Networking::EventType::FAILED_CONNECTION:
			Logger::Write<LogLevel::WARN>("Node handling failed connection for peer: ", event.peer.id);
			RemoveRoutesVia(event.peer.id);
			events.push(event);
			break;

		case Networking::EventType::DISCONNECT:
		{
			Logger::Write<LogLevel::DEBUG>("Node handling disconnect for peer: ", event.peer.id);

			auto it = s_peerToNode.find(event.peer.id);
			if (it != s_peerToNode.end())
			{
				Departure departure;
				departure.node = it->second;
				departure.ttl = ANNOUNCE_TTL;
				const auto msg = PackMessage(PacketType::DEPARTURE, Serialize(departure));
				Broadcast(msg, false, event.peer.id);
			}

			RemoveRoutesVia(event.peer.id);
			events.push(event);
			break;
		}

		case Networking::EventType::RECEIVE:
			if (!event.data.empty())
			{
				const auto type = static_cast<PacketType>(event.data[0]);
				const std::vector<std::byte> body(event.data.begin() + 1, event.data.end());

				if (type == PacketType::ANNOUNCE)
				{
					HandleAnnounce(Deserialize<Announce>(body), event.peer.id);
				}
				else if (type == PacketType::DATA)
				{
					HandleData(Deserialize<Data>(body), event.peer.id, events);
				}
				else if (type == PacketType::DEPARTURE)
				{
					HandleDeparture(Deserialize<Departure>(body), event.peer.id);
				}
				else
				{
					Logger::Write<LogLevel::WARN>("Node received unknown packet type: ", static_cast<u8>(type),
					" from peer: ", event.peer.id);
				}
			}
			break;
		}

		rawEvents.pop();
	}

	ExpireRoutes();

	const auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastAnnounce).count() >= ANNOUNCE_INTERVAL_MS)
	{
		s_lastAnnounce = now;
		SendOwnAnnuounce();
	}
}

bool Node::Send(const NodeId nodeId, const std::byte* data, const u32 size, const bool reliable)
{
	if (nodeId == 0 || nodeId == s_selfId)
	{
		Logger::Write<LogLevel::WARN>("Node tried to send to invalid node: ", nodeId);
		return false;
	}

	auto it = s_routes.find(nodeId);
	if (it == s_routes.end())
	{
		auto& q = s_pending[nodeId];
		if (q.size() >= MAX_PENDING_PER_DEST)
		{
			Logger::Write<LogLevel::WARN>("Node pending queue full for node: ", nodeId, " size: ", q.size());
			return false;
		}

		Logger::Write<LogLevel::DEBUG>("Node queued packet for node: ", nodeId, " size: ", size,
		" pending: ", q.size() + 1);
		q.emplace(data, data + size);
		return true;
	}

	Logger::Write<LogLevel::DEBUG>("Node sending packet to node: ", nodeId, " via peer: ", it->second.nextHop,
	" size: ", size, " reliable: ", reliable);

	Data dataPacket;
	dataPacket.dst = nodeId;
	dataPacket.src = s_selfId;
	dataPacket.ttl = DATA_TTL;
	dataPacket.payload.resize(size);

	if (size > 0)
	{
		std::memcpy(dataPacket.payload.data(), data, size);
	}

	const auto msg = PackMessage(PacketType::DATA, Serialize(dataPacket));
	return Networking::Send(it->second.nextHop, msg, 0, reliable);
}

bool Node::Send(const NodeId nodeId, const std::vector<std::byte>& data, const bool reliable)
{
	return Send(nodeId, data.data(), static_cast<u32>(data.size()), reliable);
}

bool Node::Send(const std::string& name, const std::byte* data, const u32 size, const bool reliable)
{
	if (name.empty())
	{
		Logger::Write<LogLevel::WARN>("Node tried to send to empty name");
		return false;
	}

	for (const auto& [id, entry] : s_routes)
	{
		if (entry.nodeId == s_selfId)
		{
			continue;
		}

		if (entry.name == name)
		{
			return Send(id, data, size, reliable);
		}
	}

	Logger::Write<LogLevel::WARN>("Node could not find route by name: ", name);
	return false;
}

bool Node::Send(const std::string& name, const std::vector<std::byte>& data, const bool reliable)
{
	return Send(name, data.data(), static_cast<u32>(data.size()), reliable);
}

std::vector<std::pair<Node::Node, Networking::PeerId>> Node::GetRoutes()
{
	std::vector<std::pair<Node, Networking::PeerId>> result;
	result.reserve(s_routes.size());

	for (const auto& [id, entry] : s_routes)
	{
		Node n;
		n.id = entry.nodeId;
		n.name = entry.name;
		result.emplace_back(n, entry.nextHop);
	}

	return result;
}

Node::NodeId Node::GetSelfId()
{
	return s_selfId;
}