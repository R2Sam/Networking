#include "Node.hpp"

#include "Networking/Networking.hpp"
#include "cereal/MyCereal.h"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"
#include "libsodium/sodium/randombytes.h"

#include <cstring>
#include <unordered_set>
#include <chrono>

namespace
{
	constexpr Networking::ChannelId ROUTING_CHANNEL = 1;
	constexpr u32 RREQ_TIMEOUT_MS    = 5000;
	constexpr u32 ROUTE_TIMEOUT_MS   = 10000;
	constexpr u32 HELLO_INTERVAL_MS  = 1000;
	constexpr u32 ANNOUNCE_INTERVAL_MS = 5000;
	constexpr u32 PENDING_TIMEOUT_MS = 5000;
	constexpr u8  DEFAULT_TTL        = 10;

	enum class PacketType : u8
	{
		RREQ,
		RREP,
		RERR,
		DATA,
		HELLO,
		ANNOUNCE
	};

	struct RREQ
	{
		Node::NodeId src = {};
		Node::NodeId dst = {};

		u32 srcSeq = {};
		u32 dstSeq = {};

		u32 reqId = {};
		u8 hopCount = {};
		u8 ttl = {};

		template <class Archive>
		void serialize(Archive& archive) // NOLINT
		{
			archive(src, dst, srcSeq, dstSeq, reqId, hopCount, ttl);
		}
	};

	struct RREP
	{
		Node::NodeId orig = {};
		Node::NodeId dst = {};

		u32 dstSeq = {};
		u8 hopCount = {};
		u32 lifetimeMs = {};

		template <class Archive>
		void serialize(Archive& archive) // NOLINT
		{
			archive(orig, dst, dstSeq, hopCount, lifetimeMs);
		}
	};

	struct RERR
	{
		std::vector<Node::NodeId> unreachable;
		u32 seq = {};

		template <class Archive>
		void serialize(Archive& archive) // NOLINT
		{
			archive(unreachable, seq);
		}
	};

	struct Data
	{
		Node::NodeId src = {};
		Node::NodeId dst = {};

		std::vector<std::byte> data;

		template <class Archive>
		void serialize(Archive& archive) // NOLINT
		{
			archive(src, dst, data);
		}
	};

	struct Hello
	{
		Node::NodeId id = {};
		u32 seq = {};
		std::string name;

		template <class Archive>
		void serialize(Archive& archive) // NOLINT
		{
			archive(id, seq, name);
		}
	};

	struct Announce
	{
		Node::NodeId id = {};
		std::string name;
		u32 seq = {};
		u8 hopCount = {};
		u8 ttl = {};

		template <class Archive>
		void serialize(Archive& archive) // NOLINT
		{
			archive(id, name, seq, hopCount, ttl);
		}
	};

	struct RouteEntry
	{
		Networking::PeerId nextHop = {};
		u32 hopCount = {};
		u32 seq = {};
		u64 expiryMs = {};
		std::unordered_set<Networking::PeerId> precursors;
	};

	struct Pending
	{
		Node::NodeId dst = {};
		std::vector<std::byte> data;
		bool reliable = true;
		u64 expireAt = {};
	};

	Node::NodeId s_localId = {};
	bool s_init = false;
	u32 s_localSeq = {};
	u32 s_nextReqId = {};
	u64 s_lastHelloMs = {};

	std::unordered_map<Node::NodeId, RouteEntry> s_routingTable;
	std::unordered_map<u64, u64> s_seenRreq;
	std::unordered_map<Node::NodeId, u32> s_lastKnownSeq;
	std::vector<Pending> s_pendingData;

	std::string s_localName;
	std::unordered_map<std::string, Node::NodeId> s_nameToId;
	std::unordered_map<Node::NodeId, std::string> s_idToName;

	u64 s_lastAnnounceMs = {};
	std::unordered_map<Node::NodeId, u32> s_lastAnnounceSeq;

	bool SeqNewer(const u32 a, const u32 b)
	{
		return static_cast<i32>(a - b) > 0;
	}

	u64 NowMs()
	{
		static const auto s_start = std::chrono::steady_clock::now();
		return static_cast<u64>(duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_start).count());
	}

	void SendPacket(const Networking::PeerId peer, const PacketType type, const std::vector<std::byte>& data, const bool reliable = true)
	{
		std::vector<std::byte> buffer;
		buffer.push_back(static_cast<std::byte>(type));
		buffer.insert(buffer.end(), data.begin(), data.end());

		Networking::Send(peer, buffer, ROUTING_CHANNEL, reliable);
	}

	void BroadcastPacket(const PacketType type, const std::vector<std::byte>& data, const bool reliable = true)
	{
		for (const Networking::PeerId peerId : Networking::GetPeerIds())
		{
			SendPacket(peerId, type, data, reliable);
		}
	}

	void UpdateRoute(const Node::NodeId dst, const Networking::PeerId nextHop, const u32 hoptCount, const u32 seq, const u64 lifetimeMs)
	{
		auto it =s_routingTable.find(dst);
		if (it == s_routingTable.end() || SeqNewer(seq, it->second.seq) || (seq == it->second.seq && hoptCount < it->second.hopCount))
		{
			RouteEntry entry;
			entry.nextHop = nextHop;
			entry.hopCount = hoptCount;
			entry.seq = seq;
			entry.expiryMs = NowMs() + lifetimeMs;

			if (it != s_routingTable.end())
			{
				entry.precursors = std::move(it->second.precursors);
			}

			s_routingTable[dst] = std::move(entry);
		}
	}

	void RemoveNameMapping(const Node::NodeId id)
	{
		auto it = s_idToName.find(id);
		if (it == s_idToName.end())
		{
			return;
		}

		// Only erase the reverse mapping if it still points at this id.
		auto jt = s_nameToId.find(it->second);
		if (jt != s_nameToId.end() && jt->second == id)
		{
			s_nameToId.erase(jt);
		}

		s_idToName.erase(it);
	}

	void RemoveRoute(const Node::NodeId dst)
	{
		auto it = s_routingTable.find(dst);
		if (it == s_routingTable.end())
		{
			return;
		}

		RERR rerr;
		rerr.unreachable = {dst};
		rerr.seq = it->second.seq + 1;

		BroadcastPacket(PacketType::RERR, Serialize(rerr), true);

		s_localSeq++;
		s_lastKnownSeq[dst] = it->second.seq + 1;

		s_routingTable.erase(it);
		RemoveNameMapping(dst);
	}

	void SendRREQ(const Node::NodeId dst)
	{
		RREQ rreq;
		rreq.src = s_localId;
		rreq.dst = dst;
		rreq.srcSeq = ++s_localSeq;
		rreq.dstSeq = s_lastKnownSeq.contains(dst) ? s_lastKnownSeq[dst] : 0;
		rreq.reqId = s_nextReqId++;
		rreq.hopCount = 0;
		rreq.ttl = DEFAULT_TTL;

		s_seenRreq[static_cast<u64>(s_localId) << 32 | rreq.reqId] = NowMs();

		BroadcastPacket(PacketType::RREQ, Serialize(rreq), false);
	}

	void SendDataVia(const Node::NodeId dst, const Networking::PeerId nextHop, const std::vector<std::byte>& data, const bool reliable)
	{
		Data dataPacket;
		dataPacket.src = s_localId;
		dataPacket.dst = dst;
		dataPacket.data = data;

		SendPacket(nextHop, PacketType::DATA, Serialize(dataPacket), reliable);

		auto it = s_routingTable.find(dst);
		if (it != s_routingTable.end())
		{
			it->second.precursors.insert(nextHop);
		}
	}

	void SendHello()
	{
		Hello hello;
		hello.id = s_localId;
		hello.seq = ++s_localSeq;
		hello.name = s_localName;

		BroadcastPacket(PacketType::HELLO, Serialize(hello), false);
	}

	void SendAnnounce()
	{
		Announce announce;
		announce.id = s_localId;
		announce.name = s_localName;
		announce.seq = ++s_localSeq;
		announce.hopCount = 0;
		announce.ttl = DEFAULT_TTL;

		s_lastAnnounceSeq[s_localId] = announce.seq;

		BroadcastPacket(PacketType::ANNOUNCE, Serialize(announce), false);
	}

	void HandleRREQ(const Networking::PeerId from, const RREQ& rreq)
	{
		const u64 key = (static_cast<u64>(rreq.src) << 32) | rreq.reqId;
		if (s_seenRreq.contains(key))
		{
			return;
		}

		s_seenRreq[key] = NowMs();

		UpdateRoute(rreq.src, from, rreq.hopCount + 1, rreq.srcSeq, ROUTE_TIMEOUT_MS);

		// Destination: send RREP
		if (rreq.dst == s_localId)
		{
			s_localSeq++;
			s_lastKnownSeq[s_localId] = s_localSeq;

			RREP rrep;
			rrep.orig = rreq.src;
			rrep.dst = s_localId;
			rrep.dstSeq = s_localSeq;
			rrep.hopCount = 0;
			rrep.lifetimeMs = ROUTE_TIMEOUT_MS;

			SendPacket(from, PacketType::RREP, Serialize(rrep), true);
			return;
		}

		auto it = s_routingTable.find(rreq.dst);
		if (it != s_routingTable.end() && it->second.expiryMs > NowMs() && !SeqNewer(rreq.dstSeq, it->second.seq))
		{
			RREP rrep;
			rrep.orig = rreq.src;
			rrep.dst  = rreq.dst;
			rrep.dstSeq = it->second.seq;
			rrep.hopCount  = static_cast<u8>(it->second.hopCount);
			rrep.lifetimeMs = static_cast<u32>(it->second.expiryMs - NowMs());

			SendPacket(from, PacketType::RREP, Serialize(rrep), true);
			return;
		}

		if (rreq.ttl > 1)
		{
			RREQ fwd = rreq;
			fwd.hopCount = rreq.hopCount + 1;
			fwd.ttl      = rreq.ttl - 1;

			BroadcastPacket(PacketType::RREQ, Serialize(fwd), false);
		}
	}

	void HandleRREP(const Networking::PeerId from, const RREP& rrep)
	{
		UpdateRoute(rrep.dst, from, rrep.hopCount + 1, rrep.dstSeq, rrep.lifetimeMs);
		s_lastKnownSeq[rrep.dst] = rrep.dstSeq;

		// We are the originator
		if (rrep.orig == s_localId)
		{
			auto routeIt = s_routingTable.find(rrep.dst);
			if (routeIt == s_routingTable.end())
			{
				return;
			}

			for (auto it = s_pendingData.begin(); it != s_pendingData.end(); )
			{
				if (it->dst == rrep.dst)
				{
					SendDataVia(rrep.dst, routeIt->second.nextHop, it->data, it->reliable);
					it = s_pendingData.erase(it);
				}

				else
				{
					++it;
				}
			}

			return;
		}

		auto it = s_routingTable.find(rrep.orig);
		if (it != s_routingTable.end())
		{
			RREP fwd = rrep;
			fwd.hopCount = rrep.hopCount + 1;
			SendPacket(it->second.nextHop, PacketType::RREP, Serialize(fwd), true);
		}
	}

	void HandleRERR(const Networking::PeerId from, const RERR& rrer)
	{
		for (Node::NodeId dst : rrer.unreachable)
		{
			auto it = s_routingTable.find(dst);
			if (it == s_routingTable.end() || it->second.nextHop != from)
			{
				continue;
			}

			RERR fwd;
			fwd.unreachable = {dst};
			fwd.seq = it->second.seq + 1;

			BroadcastPacket(PacketType::RERR, Serialize(fwd), true);

			s_lastKnownSeq[dst] = it->second.seq + 1;

			s_routingTable.erase(it);
			RemoveNameMapping(dst);
		}
	}

	void HandleData(const Networking::PeerId from, const Data& dataPacket, std::queue<Networking::Event>& events)
	{
		if (dataPacket.dst == s_localId)
		{
			Networking::Event event;
			event.type = Networking::EventType::RECEIVE;
			event.channelId = ROUTING_CHANNEL;
			event.peer.id = dataPacket.src;
			event.data = dataPacket.data;
			events.push(std::move(event));
			return;
		}

		auto it = s_routingTable.find(dataPacket.dst);
		if (it != s_routingTable.end() && it->second.expiryMs > NowMs())
		{
			it->second.precursors.insert(from);
			SendPacket(it->second.nextHop, PacketType::DATA, Serialize(dataPacket), true);
		}

		else
		{
			Pending pending;
			pending.dst = dataPacket.dst;
			pending.data = dataPacket.data;
			pending.reliable = true;
			pending.expireAt = NowMs() + PENDING_TIMEOUT_MS;

			s_pendingData.push_back(std::move(pending));
			SendRREQ(dataPacket.dst);
		}
	}

	void HandleHello(const Networking::PeerId from, const Hello& hello)
	{
		for (auto& [dst, entry] : s_routingTable)
		{
			if (entry.nextHop == from)
			{
				entry.expiryMs = NowMs() + ROUTE_TIMEOUT_MS;
			}
		}

		if (hello.id != s_localId)
	    {
	        UpdateRoute(hello.id, from, 1, hello.seq, ROUTE_TIMEOUT_MS);
	        s_lastKnownSeq[hello.id] = hello.seq;
	    }

	    if (hello.id != s_localId && !hello.name.empty())
	    {
	    	auto idIt = s_idToName.find(hello.id);
	    	if (idIt != s_idToName.end() && idIt->second != hello.name)
	    	{
	    		s_nameToId.erase(idIt->second);
	    		s_idToName.erase(idIt);
	    	}

	    	auto nameIt = s_nameToId.find(hello.name);
	    	if (nameIt != s_nameToId.end() && nameIt->second != hello.id)
	    	{
	    		s_idToName.erase(nameIt->second);
	    	}

	    	s_nameToId[hello.name] = hello.id;
	    	s_idToName[hello.id] = hello.name;
	    }
	}

	void HandleAnnounce(const Networking::PeerId from, const Announce& announce)
	{
		if (announce.id != s_localId)
		{
			UpdateRoute(announce.id, from, announce.hopCount + 1, announce.seq, ROUTE_TIMEOUT_MS);
			s_lastKnownSeq[announce.id] = announce.seq;
		}

		if (announce.id != s_localId && !announce.name.empty())
		{
			auto idIt = s_idToName.find(announce.id);
			if (idIt != s_idToName.end() && idIt->second != announce.name)
			{
				s_nameToId.erase(idIt->second);
				s_idToName.erase(idIt);
			}

			auto nameIt = s_nameToId.find(announce.name);
			if (nameIt != s_nameToId.end() && nameIt->second != announce.id)
			{
				s_idToName.erase(nameIt->second);
			}

			s_nameToId[announce.name] = announce.id;
			s_idToName[announce.id] = announce.name;
		}

		auto it = s_lastAnnounceSeq.find(announce.id);
		if (it != s_lastAnnounceSeq.end() && !SeqNewer(announce.seq, it->second))
		{
			return;
		}
		s_lastAnnounceSeq[announce.id] = announce.seq;

		if (announce.ttl > 1)
		{
			Announce fwd = announce;
			fwd.hopCount = announce.hopCount + 1;
			fwd.ttl = announce.ttl - 1;

			BroadcastPacket(PacketType::ANNOUNCE, Serialize(fwd), false);
		}
	}

	void Cleanup()
	{
		const u64 time = NowMs();

		for (auto it = s_routingTable.begin(); it != s_routingTable.end(); )
		{
			if (it->second.expiryMs <= time)
			{
				RemoveNameMapping(it->first);
				it = s_routingTable.erase(it);
			}

			else
			{
				++it;
			}
		}

		for (auto it = s_seenRreq.begin(); it != s_seenRreq.end(); )
		{
			if (time - it->second > RREQ_TIMEOUT_MS * 2)
			{
				it = s_seenRreq.erase(it);
			}

			else
			{
				++it;
			}
		}

		for (auto it = s_pendingData.begin(); it != s_pendingData.end(); )
		{
			if (it->expireAt <= time)
			{
				it = s_pendingData.erase(it);
			}

			else
			{
				++it;
			}
		}

		if (time - s_lastHelloMs >= HELLO_INTERVAL_MS)
		{
			SendHello();
			s_lastHelloMs = time;
		}

		if (time - s_lastAnnounceMs >= ANNOUNCE_INTERVAL_MS)
		{
			SendAnnounce();
			s_lastAnnounceMs = time;
		}
	}
}

bool Node::Init(const u16 port, const std::string& name)
{
	if (s_init)
	{
		return false;
	}

	if (!Networking::InitServer(port, 100, 2))
	{
		return false;
	}

	s_init = true;

	std::byte buffer[8] = {};
	randombytes_buf(buffer, 8);
	memcpy(&s_localId, buffer, 8);

	s_localName = name;

	s_lastHelloMs = NowMs();
	SendAnnounce();
	s_lastAnnounceMs = NowMs();

	return true;
}

void Node::Shutdown()
{
	Networking::Shutdown();

	s_routingTable.clear();
	s_seenRreq.clear();
	s_lastKnownSeq.clear();
	s_pendingData.clear();
	s_nameToId.clear();
	s_idToName.clear();
	s_lastAnnounceSeq.clear();
	s_localName.clear();

	s_init = false;
}

std::optional<Networking::PeerId> Node::Connect(const Networking::Address& address)
{
	return Networking::Connect(address);
}

std::optional<Networking::PeerId> Node::Connect(const std::string& ip, const u16 port)
{
	return Networking::Connect(ip, port);
}

void Node::Disconnect(const Networking::PeerId peerId)
{
	Networking::Disconnect(peerId);
}

void Node::Poll(std::queue<Networking::Event>& events, const u32 timeoutMs) // NOLINT
{
	if (!s_init)
	{
		return;
	}

	std::queue<Networking::Event> rawEvents;
	Networking::Poll(rawEvents, timeoutMs);

	while(!rawEvents.empty())
	{
		Networking::Event event = std::move(rawEvents.front());
		rawEvents.pop();

		if (event.type == Networking::EventType::RECEIVE && event.channelId == ROUTING_CHANNEL)
		{
			if (event.data.empty())
			{
				continue;
			}

			const PacketType type = static_cast<PacketType>(event.data[0]);
			std::vector<std::byte> body(event.data.begin() + 1, event.data.end());

			switch (type)
			{
				case PacketType::RREQ : HandleRREQ(event.peer.id, Deserialize<RREQ>(body)); break;
				case PacketType::RREP: HandleRREP(event.peer.id, Deserialize<RREP>(body)); break;
				case PacketType::RERR: HandleRERR(event.peer.id, Deserialize<RERR>(body)); break;
				case PacketType::DATA: HandleData(event.peer.id, Deserialize<Data>(body), events); break;
				case PacketType::HELLO: HandleHello(event.peer.id, Deserialize<Hello>(body)); break;
				case PacketType::ANNOUNCE: HandleAnnounce(event.peer.id, Deserialize<Announce>(body)); break;
				default: break;
			}
		}

		else
		{
			events.push(std::move(event));

			if (event.type == Networking::EventType::CONNECT)
			{
				SendAnnounce();
			}

			if (event.type == Networking::EventType::DISCONNECT)
			{
				std::vector<NodeId> broken;

				for (auto& [dst, entry] : s_routingTable)
				{
					if (entry.nextHop == event.peer.id)
					{
						broken.push_back(dst);
					}
				}

				for (const NodeId id : broken)
				{
					RemoveRoute(id);
				}
			}
		}
	}

	Cleanup();
}

bool Node::Send(const NodeId nodeId, const std::byte* data, const u32 size, const bool reliable)
{
	if (!s_init)
	{
		return false;
	}

	std::vector<std::byte> vector(data, data + size);

	auto it = s_routingTable.find(nodeId);
	if (it != s_routingTable.end() && it->second.expiryMs > NowMs())
	{
		SendDataVia(nodeId, it->second.nextHop, vector, reliable);
		return true;
	}

	Pending pending;
	pending.dst = nodeId;
	pending.data = vector;
	pending.reliable = reliable;
	pending.expireAt = NowMs() + PENDING_TIMEOUT_MS;
	s_pendingData.push_back(std::move(pending));

	SendRREQ(nodeId);
	return true;
}

bool Node::Send(const NodeId nodeId, const std::vector<std::byte>& data, const bool reliable)
{
	return Send(nodeId, data.data(), data.size(), reliable);
}

bool Node::Send(const std::string& name, const std::byte* data, const u32 size, const bool reliable)
{
	auto it = s_nameToId.find(name);
	if (it == s_nameToId.end())
	{
		return false;
	}

	return Send(it->second, data, size, reliable);
}

bool Node::Send(const std::string& name, const std::vector<std::byte>& data, const bool reliable)
{
	auto it = s_nameToId.find(name);
	if (it == s_nameToId.end())
	{
		return false;
	}

	return Send(it->second, data, reliable);
}

std::vector<std::pair<Node::Node, Networking::PeerId>> Node::GetRoutes()
{
	std::vector<std::pair<Node, Networking::PeerId>> vector;
	vector.reserve(s_routingTable.size());

	for (auto& [dst, entry] : s_routingTable)
	{
		auto it = s_idToName.find(dst);
		if (it != s_idToName.end())
		{
			vector.emplace_back(Node{dst, s_idToName[dst]}, entry.nextHop);
		}

		else
		{
			vector.emplace_back(Node{dst, ""}, entry.nextHop);
		}
	}

	return vector;
}
