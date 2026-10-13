#include "Networking/Transport.hpp"
#include "Log/Logger.hpp"
#include "cereal/MyCereal.h"
#include "libsodium/sodium/randombytes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <queue>
#include <unordered_map>
#include <vector>

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

namespace
{
	using NodeId = Transport::NodeId;
	using PeerId = Transport::PeerId;
	using ChannelId = Transport::ChannelId;

	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	constexpr u8 TAG_TRANSPORT = 0x00;
	constexpr u8 TAG_RAW = 0x01;

	constexpr u32 MSS = 1024;
	constexpr u32 WINDOW_SIZE = 64;
	constexpr u32 MAX_PENDING_MESSAGES = 256;
	constexpr u32 MAX_REASSEMBLY_BUFFERS = 64;
	constexpr u32 MAX_CWND = 256;
	constexpr u32 MIN_CWND = 1;

	constexpr u32 INITIAL_RTO_MS = 1000;
	constexpr u32 MIN_RTO_MS = 100;
	constexpr u32 MAX_RTO_MS = 4000;
	constexpr u32 MAX_RETRANSMITS = 10;
	constexpr u32 FAST_RETRANSMIT_THRESHOLD = 3;

	constexpr u32 KEEPALIVE_INTERVAL_MS = 2000;
	constexpr u32 SESSION_TIMEOUT_MS = 10000;
	// constexpr u32 HANDSHAKE_TIMEOUT_MS = 5000;
	constexpr u32 REASSEMBLY_TIMEOUT_MS = 30000;
	constexpr u32 CLOSE_LINGER_MS = 2000;
	constexpr u32 FIN_TIMEOUT_MS = 5000;

	constexpr u32 INITIAL_SSTHRESH = 64;

	enum class State : u8
	{
		SYN_SENT,
		SYN_RECEIVED,
		ESTABLISHED,
		FIN_WAIT,
		CLOSING,
		CLOSED,
	};

	enum class PacketType : u8
	{
		SYN = 1,
		SYN_ACK = 2,
		DATA = 3,
		ACK = 4,
		FIN = 5,
		RST = 6,
		PING = 7,
	};

	struct Packet
	{
		PacketType type = PacketType::DATA;
		NodeId src = 0;
		NodeId dst = 0;
		u8 channel = 0;
		u16 seq = 0;
		u16 ack = 0;
		u32 ackBits = 0;
		u16 window = 0;
		u32 msgId = 0;
		u16 fragIdx = 0;
		u16 fragCount = 1;
		std::vector<u8> payload;

		template <class Archive>
		void serialize(Archive& ar) // NOLINT
		{
			ar(type, src, dst, channel, seq, ack, ackBits, window, msgId, fragIdx, fragCount, payload);
		}
	};

	struct UnackedPacket
	{
		u16 seq = 0;
		u8 channel = 0;
		std::vector<std::byte> serialized;
		TimePoint sendTime;
		u32 retransmits = 0;
	};

	struct PendingPacket
	{
		u32 msgId = 0;
		u16 fragIdx = 0;
		u16 fragCount = 1;
		std::vector<u8> payload;
	};

	struct ReassemblyBuffer
	{
		u16 fragCount = 0;
		u16 received = 0;
		std::vector<std::vector<u8>> fragments;
		TimePoint lastUpdate;
	};

	struct ChannelState
	{
		u16 localSeq = 0;
		u16 remoteSeq = 0;
		u16 remoteWindow = WINDOW_SIZE;
		u16 lastRemoteAck = 0;
		bool hasRemoteAck = false;
		u32 dupAcks = 0;
		std::deque<PendingPacket> pending;
		std::deque<UnackedPacket> unacked;
		std::unordered_map<u16, Packet> outOfOrder;
		std::unordered_map<u32, ReassemblyBuffer> reassembly;
	};

	struct Session
	{
		NodeId remote = 0;
		State state = State::CLOSED;
		u16 localIsn = 0;
		u16 remoteIsn = 0;

		std::unordered_map<ChannelId, ChannelState> channels;

		f32 srtt = 0;
		f32 rttvar = 0;
		f32 rto = static_cast<f32>(INITIAL_RTO_MS);
		bool rttInitialized = false;

		f32 cwnd = MIN_CWND;
		f32 ssthresh = INITIAL_SSTHRESH;
		u32 nextMsgId = 1;

		TimePoint lastRecvTime;
		TimePoint lastSendTime;
		TimePoint synSentTime;
		u32 handshakeRetries = 0;

		bool finSent = false;
		u16 finSeq = 0;
		TimePoint finSentTime;
		TimePoint closedTime;
	};

	NodeId s_selfId = 0;
	std::unordered_map<NodeId, Session> s_sessions;

	inline bool SeqGreater(const u16 a, const u16 b)
	{
		return a != b && static_cast<u16>(a - b) < 32768;
	}

	u16 RandomIsn()
	{
		u16 isn = 0;
		randombytes(reinterpret_cast<u8*>(&isn), sizeof(isn));
		return isn;
	}

	std::vector<std::byte> SerializeTransport(const Packet& pkt)
	{
		const auto body = Serialize(pkt);
		std::vector<std::byte> out;
		out.reserve(body.size() + 1);
		out.push_back(static_cast<std::byte>(TAG_TRANSPORT));
		out.insert(out.end(), body.begin(), body.end());
		return out;
	}

	std::vector<std::byte> SerializeRaw(const std::byte* data, const u32 size)
	{
		std::vector<std::byte> out;
		out.reserve(size + 9);
		out.push_back(static_cast<std::byte>(TAG_RAW));

		std::byte idBytes[8];
		std::memcpy(idBytes, &s_selfId, sizeof(idBytes));
		out.insert(out.end(), idBytes, idBytes + sizeof(idBytes));
		out.insert(out.end(), data, data + size);

		return out;
	}

	u32 ComputeAckBits(const ChannelState& cs)
	{
		u32 bits = 0;

		for (const auto& [seq, _] : cs.outOfOrder)
		{
			if (!SeqGreater(seq, cs.remoteSeq))
			{
				continue;
			}

			const u32 distance = static_cast<u16>(seq - cs.remoteSeq);
			if (distance == 0 || distance > 32)
			{
				continue;
			}

			bits |= (1u << (distance - 1));
		}

		return bits;
	}

	u16 ComputeWindow(const ChannelState& cs)
	{
		const u32 used = static_cast<u32>(cs.outOfOrder.size());
		if (used >= WINDOW_SIZE)
		{
			return 0;
		}

		return static_cast<u16>(WINDOW_SIZE - used);
	}

	ChannelState& GetChannel(Session& session, const ChannelId channel)
	{
		return session.channels[channel];
	}

	void UpdateRtt(Session& session, const f32 sampleMs)
	{
		if (!session.rttInitialized)
		{
			session.srtt = sampleMs;
			session.rttvar = sampleMs * 0.5f;
			session.rttInitialized = true;
		}
		else
		{
			session.rttvar = (0.75f * session.rttvar) + (0.25f * std::fabs(session.srtt - sampleMs));
			session.srtt = (0.875f * session.srtt) + (0.125f * sampleMs);
		}

		session.rto = session.srtt + (4.0f * session.rttvar);
		session.rto = std::clamp(session.rto, static_cast<f32>(MIN_RTO_MS), static_cast<f32>(MAX_RTO_MS));
	}

	void ApplyCongestionAck(Session& session, const u32 ackedCount)
	{
		if (ackedCount == 0)
		{
			return;
		}

		if (session.cwnd < session.ssthresh)
		{
			session.cwnd += static_cast<f32>(ackedCount);
		}
		else
		{
			session.cwnd += static_cast<f32>(ackedCount) / session.cwnd;
		}

		session.cwnd = std::min(session.cwnd, static_cast<f32>(MAX_CWND));
	}

	void ApplyCongestionLoss(Session& session)
	{
		session.ssthresh = std::max(session.cwnd * 0.5f, static_cast<f32>(2));
		session.cwnd = MIN_CWND;
	}

	u32 TotalUnacked(const Session& session)
	{
		u32 total = 0;
		for (const auto& [_, cs] : session.channels)
		{
			total += static_cast<u32>(cs.unacked.size());
		}
		return total;
	}

	void TransmitTracked(Session& session, const std::vector<std::byte>& bytes, const ChannelId channel, const u16 seq)
	{
		auto& cs = GetChannel(session, channel);
		cs.unacked.push_back({seq, channel, bytes, Clock::now(), 0});

		Node::Send(session.remote, bytes, false);
		session.lastSendTime = Clock::now();
	}

	void TransmitControl(Session& session, const Packet& pkt)
	{
		const auto bytes = SerializeTransport(pkt);
		Node::Send(session.remote, bytes, false);
		session.lastSendTime = Clock::now();
	}

	void SendAck(Session& session, const ChannelId channel)
	{
		auto& cs = GetChannel(session, channel);

		Packet pkt;
		pkt.type = PacketType::ACK;
		pkt.src = s_selfId;
		pkt.dst = session.remote;
		pkt.channel = channel;
		pkt.seq = cs.localSeq;
		pkt.ack = cs.remoteSeq;
		pkt.ackBits = ComputeAckBits(cs);
		pkt.window = ComputeWindow(cs);

		TransmitControl(session, pkt);
	}

	void SendSyn(Session& session)
	{
		Packet pkt;
		pkt.type = PacketType::SYN;
		pkt.src = s_selfId;
		pkt.dst = session.remote;
		pkt.seq = session.localIsn;

		TransmitControl(session, pkt);
		session.synSentTime = Clock::now();
	}

	void SendSynAck(Session& session)
	{
		Packet pkt;
		pkt.type = PacketType::SYN_ACK;
		pkt.src = s_selfId;
		pkt.dst = session.remote;
		pkt.seq = session.localIsn;
		pkt.ack = static_cast<u16>(session.remoteIsn + 1);

		TransmitControl(session, pkt);
		session.synSentTime = Clock::now();
	}

	void SendFin(Session& session)
	{
		auto& cs = GetChannel(session, 0);

		const u16 seq = cs.localSeq++;

		Packet pkt;
		pkt.type = PacketType::FIN;
		pkt.src = s_selfId;
		pkt.dst = session.remote;
		pkt.channel = 0;
		pkt.seq = seq;
		pkt.ack = cs.remoteSeq;
		pkt.ackBits = ComputeAckBits(cs);
		pkt.window = ComputeWindow(cs);

		const auto bytes = SerializeTransport(pkt);
		TransmitTracked(session, bytes, 0, seq);

		session.finSent = true;
		session.finSeq = seq;
		session.finSentTime = Clock::now();
	}

	void SendRst(Session& session)
	{
		Packet pkt;
		pkt.type = PacketType::RST;
		pkt.src = s_selfId;
		pkt.dst = session.remote;

		TransmitControl(session, pkt);
	}

	void SendPing(Session& session)
	{
		Packet pkt;
		pkt.type = PacketType::PING;
		pkt.src = s_selfId;
		pkt.dst = session.remote;

		TransmitControl(session, pkt);
	}

	void FlushPending(Session& session, const ChannelId channel)
	{
		auto& cs = GetChannel(session, channel);

		const f32 wndLimit = std::min(session.cwnd, static_cast<f32>(cs.remoteWindow));
		const u32 limit = static_cast<u32>(std::max(wndLimit, 0.0f));

		while (!cs.pending.empty() && TotalUnacked(session) < limit)
		{
			auto& msg = cs.pending.front();

			const u16 seq = cs.localSeq++;

			Packet pkt;
			pkt.type = PacketType::DATA;
			pkt.src = s_selfId;
			pkt.dst = session.remote;
			pkt.channel = channel;
			pkt.seq = seq;
			pkt.ack = cs.remoteSeq;
			pkt.ackBits = ComputeAckBits(cs);
			pkt.window = ComputeWindow(cs);
			pkt.msgId = msg.msgId;
			pkt.fragIdx = msg.fragIdx;
			pkt.fragCount = msg.fragCount;
			pkt.payload = msg.payload;

			const auto bytes = SerializeTransport(pkt);
			TransmitTracked(session, bytes, channel, seq);

			cs.pending.pop_front();
		}
	}

	void FlushAllPending(Session& session)
	{
		for (auto& [channel, _] : session.channels)
		{
			FlushPending(session, channel);
		}
	}

	void ProcessAck(Session& session, const Packet& pkt)
	{
		auto& cs = GetChannel(session, pkt.channel);

		const u16 ack = pkt.ack;
		const u32 ackBits = pkt.ackBits;

		u32 newlyAcked = 0;

		while (!cs.unacked.empty())
		{
			const auto& front = cs.unacked.front();
			const bool acked = !SeqGreater(front.seq, ack);
			const u32 distance = static_cast<u16>(front.seq - ack);

			const bool sackAcked = distance > 0 && distance <= 32 && (ackBits & (1u << (distance - 1))) != 0;

			if (!acked && !sackAcked)
			{
				break;
			}

			const f32 sampleMs =
			std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(Clock::now() - front.sendTime).count();
			UpdateRtt(session, sampleMs);

			cs.unacked.pop_front();
			newlyAcked++;
		}

		if (newlyAcked > 0)
		{
			cs.dupAcks = 0;
			cs.lastRemoteAck = ack;
			cs.hasRemoteAck = true;
			ApplyCongestionAck(session, newlyAcked);
			FlushPending(session, pkt.channel);
			return;
		}

		if (!cs.hasRemoteAck || ack != cs.lastRemoteAck)
		{
			cs.lastRemoteAck = ack;
			cs.hasRemoteAck = true;
			cs.dupAcks = 0;
			return;
		}

		cs.dupAcks++;

		if (cs.dupAcks == FAST_RETRANSMIT_THRESHOLD && !cs.unacked.empty())
		{
			auto& oldest = cs.unacked.front();
			Logger::Write<LogLevel::WARN>("Transport fast retransmit to node: ", session.remote,
			" channel: ", pkt.channel, " seq: ", oldest.seq);

			ApplyCongestionLoss(session);
			Node::Send(session.remote, oldest.serialized, false);
			oldest.sendTime = Clock::now();
			oldest.retransmits++;
		}
	}

	void ProcessAckAndTransitions(Session& session, const Packet& pkt, std::queue<Transport::Event>& out)
	{
		ProcessAck(session, pkt);

		if (session.state == State::SYN_RECEIVED)
		{
			Logger::Write<LogLevel::INFO>("Transport session established with node: ", session.remote);
			session.state = State::ESTABLISHED;

			Transport::Event ev;
			ev.type = Transport::EventType::CONNECTED;
			ev.nodeId = session.remote;
			out.push(std::move(ev));

			FlushAllPending(session);
			return;
		}

		if ((session.state == State::FIN_WAIT || session.state == State::CLOSING) && session.finSent &&
			!SeqGreater(session.finSeq, pkt.ack))
		{
			Logger::Write<LogLevel::INFO>("Transport session close confirmed by node: ", session.remote);
			session.state = State::CLOSED;
			session.closedTime = Clock::now();

			Transport::Event ev;
			ev.type = Transport::EventType::DISCONNECTED;
			ev.nodeId = session.remote;
			out.push(std::move(ev));
		}
	}

	void DeliverInOrder(Session& session, const Packet& pkt, std::queue<Transport::Event>& out)
	{
		auto& cs = GetChannel(session, pkt.channel);

		if (pkt.fragCount <= 1)
		{
			Transport::Event ev;
			ev.type = Transport::EventType::RECEIVE;
			ev.nodeId = session.remote;
			ev.channelId = pkt.channel;
			ev.data.resize(pkt.payload.size());
			if (!pkt.payload.empty())
			{
				std::memcpy(ev.data.data(), pkt.payload.data(), pkt.payload.size());
			}
			out.push(std::move(ev));
			return;
		}

		if (cs.reassembly.size() >= MAX_REASSEMBLY_BUFFERS && !cs.reassembly.contains(pkt.msgId))
		{
			Logger::Write<LogLevel::WARN>("Transport dropping fragment for node: ", session.remote,
			" msgId: ", pkt.msgId, " reassembly buffer full");
			return;
		}

		auto& buf = cs.reassembly[pkt.msgId];
		if (buf.fragCount == 0)
		{
			buf.fragCount = pkt.fragCount;
			buf.fragments.resize(pkt.fragCount);
		}

		if (pkt.fragIdx >= buf.fragments.size())
		{
			return;
		}

		if (buf.fragments[pkt.fragIdx].empty() && !pkt.payload.empty())
		{
			buf.fragments[pkt.fragIdx] = pkt.payload;
			buf.received++;
		}

		buf.lastUpdate = Clock::now();

		if (buf.received < buf.fragCount)
		{
			return;
		}

		std::vector<u8> assembled;
		for (auto& frag : buf.fragments)
		{
			assembled.insert(assembled.end(), frag.begin(), frag.end());
		}

		Transport::Event ev;
		ev.type = Transport::EventType::RECEIVE;
		ev.nodeId = session.remote;
		ev.channelId = pkt.channel;
		ev.data.resize(assembled.size());
		if (!assembled.empty())
		{
			std::memcpy(ev.data.data(), assembled.data(), assembled.size());
		}
		out.push(std::move(ev));

		cs.reassembly.erase(pkt.msgId);
	}

	void AdvanceReceiveWindow(Session& session, const ChannelId channel, std::queue<Transport::Event>& out)
	{
		auto& cs = GetChannel(session, channel);

		while (true)
		{
			auto it = cs.outOfOrder.find(cs.remoteSeq);
			if (it == cs.outOfOrder.end())
			{
				break;
			}

			const Packet pkt = it->second;
			cs.outOfOrder.erase(it);
			cs.remoteSeq++;

			DeliverInOrder(session, pkt, out);
		}
	}

	void HandleData(Session& session, const Packet& pkt, std::queue<Transport::Event>& out)
	{
		auto& cs = GetChannel(session, pkt.channel);

		if (pkt.seq == cs.remoteSeq)
		{
			cs.remoteSeq++;
			DeliverInOrder(session, pkt, out);
			AdvanceReceiveWindow(session, pkt.channel, out);
		}

		else if (SeqGreater(pkt.seq, cs.remoteSeq))
		{
			if (cs.outOfOrder.size() >= WINDOW_SIZE)
			{
				Logger::Write<LogLevel::WARN>("Transport receive buffer full from node: ", session.remote,
				" channel: ", pkt.channel);
				return;
			}

			cs.outOfOrder[pkt.seq] = pkt;
		}

		SendAck(session, pkt.channel);
	}

	void HandleSyn(Session& session, const Packet& pkt, [[maybe_unused]] std::queue<Transport::Event>& out)
	{
		session.remoteIsn = pkt.seq;

		if (session.state == State::SYN_RECEIVED || session.state == State::ESTABLISHED ||
			session.state == State::FIN_WAIT || session.state == State::CLOSING)
		{
			SendSynAck(session);
			return;
		}

		if (session.state == State::SYN_SENT)
		{
			Logger::Write<LogLevel::DEBUG>("Transport simultaneous open with node: ", session.remote);
			SendSynAck(session);
			session.state = State::SYN_RECEIVED;
			return;
		}

		Logger::Write<LogLevel::DEBUG>("Transport accepted SYN from node: ", session.remote,
		" localIsn: ", session.localIsn, " remoteIsn: ", session.remoteIsn);

		SendSynAck(session);
		session.state = State::SYN_RECEIVED;
	}

	void HandleSynAck(Session& session, const Packet& pkt, std::queue<Transport::Event>& out)
	{
		session.remoteIsn = static_cast<u16>(pkt.seq);

		if (session.state == State::SYN_SENT)
		{
			Logger::Write<LogLevel::INFO>("Transport session established with node: ", session.remote);
			session.state = State::ESTABLISHED;

			Packet ackPkt;
			ackPkt.type = PacketType::ACK;
			ackPkt.src = s_selfId;
			ackPkt.dst = session.remote;
			TransmitControl(session, ackPkt);

			Transport::Event ev;
			ev.type = Transport::EventType::CONNECTED;
			ev.nodeId = session.remote;
			out.push(std::move(ev));

			FlushAllPending(session);
			return;
		}

		if (session.state == State::SYN_RECEIVED)
		{
			Logger::Write<LogLevel::INFO>("Transport session established with node: ", session.remote);
			session.state = State::ESTABLISHED;

			Packet ackPkt;
			ackPkt.type = PacketType::ACK;
			ackPkt.src = s_selfId;
			ackPkt.dst = session.remote;
			TransmitControl(session, ackPkt);

			Transport::Event ev;
			ev.type = Transport::EventType::CONNECTED;
			ev.nodeId = session.remote;
			out.push(std::move(ev));

			FlushAllPending(session);
		}
	}

	void HandleFin(Session& session, const Packet& pkt, std::queue<Transport::Event>& out)
	{
		auto& cs = GetChannel(session, 0);

		if (pkt.seq == cs.remoteSeq)
		{
			cs.remoteSeq++;
		}

		SendAck(session, 0);

		if (session.state == State::CLOSED)
		{
			return;
		}

		if (session.state == State::FIN_WAIT)
		{
			Logger::Write<LogLevel::INFO>("Transport simultaneous close with node: ", session.remote);
			session.state = State::CLOSING;
			return;
		}

		Logger::Write<LogLevel::INFO>("Transport session closed by peer: ", session.remote);
		session.state = State::CLOSED;
		session.closedTime = Clock::now();

		Transport::Event ev;
		ev.type = Transport::EventType::DISCONNECTED;
		ev.nodeId = session.remote;
		out.push(std::move(ev));
	}

	void HandleRst(Session& session, [[maybe_unused]] const Packet& pkt, std::queue<Transport::Event>& out)
	{
		Logger::Write<LogLevel::WARN>("Transport received RST from node: ", session.remote);
		session.state = State::CLOSED;
		session.closedTime = Clock::now();

		Transport::Event ev;
		ev.type = Transport::EventType::DISCONNECTED;
		ev.nodeId = session.remote;
		out.push(std::move(ev));
	}

	void HandlePing(Session& session, const Packet& pkt)
	{
		SendAck(session, pkt.channel);
	}

	void HandleTransportPacket(std::queue<Transport::Event>& out, const std::vector<std::byte>& data,
	[[maybe_unused]] const NodeId from)
	{
		if (data.size() <= 1)
		{
			return;
		}

		const auto body = std::vector<std::byte>(data.begin() + 1, data.end());
		const Packet pkt = Deserialize<Packet>(body);

		if (pkt.src == 0 || pkt.src == s_selfId)
		{
			Logger::Write<LogLevel::WARN>("Transport dropping packet with invalid src: ", pkt.src);
			return;
		}

		if (pkt.dst != 0 && pkt.dst != s_selfId)
		{
			Logger::Write<LogLevel::WARN>("Transport dropping packet destined for: ", pkt.dst);
			return;
		}

		auto it = s_sessions.find(pkt.src);

		if (it == s_sessions.end())
		{
			if (pkt.type != PacketType::SYN)
			{
				Logger::Write<LogLevel::WARN>("Transport dropping non-SYN from unknown node: ", pkt.src);
				return;
			}

			Session fresh;
			fresh.remote = pkt.src;
			fresh.localIsn = RandomIsn();
			fresh.state = State::SYN_RECEIVED;
			fresh.lastRecvTime = Clock::now();
			fresh.lastSendTime = Clock::now();

			auto [insertedIt, _] = s_sessions.emplace(pkt.src, std::move(fresh));
			HandleSyn(insertedIt->second, pkt, out);
			return;
		}

		Session& session = it->second;
		session.lastRecvTime = Clock::now();

		switch (pkt.type)
		{
		case PacketType::SYN:
			HandleSyn(session, pkt, out);
			break;

		case PacketType::SYN_ACK:
			HandleSynAck(session, pkt, out);
			break;

		case PacketType::ACK:
			ProcessAckAndTransitions(session, pkt, out);
			break;

		case PacketType::DATA:
			ProcessAckAndTransitions(session, pkt, out);
			HandleData(session, pkt, out);
			break;

		case PacketType::FIN:
			ProcessAckAndTransitions(session, pkt, out);
			HandleFin(session, pkt, out);
			break;

		case PacketType::RST:
			HandleRst(session, pkt, out);
			break;

		case PacketType::PING:
			ProcessAckAndTransitions(session, pkt, out);
			HandlePing(session, pkt);
			break;

		default:
			break;
		}
	}

	void HandleRawPacket(std::queue<Transport::Event>& out, const std::vector<std::byte>& data)
	{
		if (data.size() < 9)
		{
			Logger::Write<LogLevel::WARN>("Transport received malformed raw packet");
			return;
		}

		NodeId src = 0;
		std::memcpy(&src, data.data() + 1, sizeof(src));

		if (src == 0)
		{
			Logger::Write<LogLevel::WARN>("Transport received raw packet with zero src");
			return;
		}

		Transport::Event ev;
		ev.type = Transport::EventType::RECEIVE;
		ev.nodeId = src;
		ev.channelId = 0;
		ev.data.assign(data.begin() + 9, data.end());
		out.push(std::move(ev));
	}

	void RetransmitExpired(Session& session)
	{
		const auto now = Clock::now();
		const auto rtoMs = session.rto;

		for (auto& [channel, cs] : session.channels)
		{
			for (auto& unacked : cs.unacked)
			{
				const f32 ageMs =
				std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(now - unacked.sendTime).count();

				if (ageMs < rtoMs)
				{
					continue;
				}

				if (unacked.retransmits >= MAX_RETRANSMITS)
				{
					Logger::Write<LogLevel::ERROR>("Transport session to node: ", session.remote,
					" exceeded max retransmits, sending RST");
					SendRst(session);
					session.state = State::CLOSED;
					session.closedTime = Clock::now();
					return;
				}

				Logger::Write<LogLevel::DEBUG>("Transport retransmit to node: ", session.remote, " channel: ", channel,
				" seq: ", unacked.seq, " attempt: ", unacked.retransmits + 1);

				Node::Send(session.remote, unacked.serialized, false);
				unacked.sendTime = now;
				unacked.retransmits++;

				ApplyCongestionLoss(session);
			}
		}
	}

	void UpdateTimers(std::queue<Transport::Event>& out) // NOLINT
	{
		const auto now = Clock::now();

		for (auto it = s_sessions.begin(); it != s_sessions.end();)
		{
			Session& session = it->second;

			if (session.state == State::CLOSED)
			{
				const f32 closedAgeMs =
				std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(now - session.closedTime).count();

				if (closedAgeMs >= CLOSE_LINGER_MS)
				{
					it = s_sessions.erase(it);
					continue;
				}

				it++;
				continue;
			}

			const f32 idleMs =
			std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(now - session.lastRecvTime).count();

			const f32 sinceSendMs =
			std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(now - session.lastSendTime).count();

			const bool handshake = session.state == State::SYN_SENT || session.state == State::SYN_RECEIVED;

			if (handshake)
			{
				const f32 synAgeMs =
				std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(now - session.synSentTime).count();

				if (synAgeMs >= session.rto)
				{
					if (session.handshakeRetries >= MAX_RETRANSMITS)
					{
						Logger::Write<LogLevel::ERROR>("Transport handshake to node: ", session.remote,
						" failed after ", session.handshakeRetries, " retries");

						Transport::Event ev;
						ev.type = Transport::EventType::FAILED;
						ev.nodeId = session.remote;
						out.push(std::move(ev));

						it = s_sessions.erase(it);
						continue;
					}

					session.handshakeRetries++;
					session.synSentTime = now;

					if (session.state == State::SYN_SENT)
					{
						SendSyn(session);
					}
					else
					{
						SendSynAck(session);
					}
				}

				it++;
				continue;
			}

			if (session.state == State::FIN_WAIT || session.state == State::CLOSING)
			{
				const f32 finAgeMs =
				std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(now - session.finSentTime).count();

				if (finAgeMs >= FIN_TIMEOUT_MS)
				{
					Logger::Write<LogLevel::WARN>("Transport FIN to node: ", session.remote, " timed out after ",
					finAgeMs, " ms, forcing close");

					Transport::Event ev;
					ev.type = Transport::EventType::DISCONNECTED;
					ev.nodeId = session.remote;
					out.push(std::move(ev));

					it = s_sessions.erase(it);
					continue;
				}
			}

			if (idleMs >= SESSION_TIMEOUT_MS)
			{
				Logger::Write<LogLevel::WARN>("Transport session to node: ", session.remote, " timed out after ",
				idleMs, " ms");

				Transport::Event ev;
				ev.type = Transport::EventType::DISCONNECTED;
				ev.nodeId = session.remote;
				out.push(std::move(ev));

				it = s_sessions.erase(it);
				continue;
			}

			if (session.state == State::ESTABLISHED && sinceSendMs >= KEEPALIVE_INTERVAL_MS)
			{
				SendPing(session);
			}

			RetransmitExpired(session);

			for (auto& [channel, cs] : session.channels)
			{
				for (auto rit = cs.reassembly.begin(); rit != cs.reassembly.end();)
				{
					const f32 ageMs =
					std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(now - rit->second.lastUpdate)
					.count();

					if (ageMs > REASSEMBLY_TIMEOUT_MS)
					{
						Logger::Write<LogLevel::WARN>("Transport dropping stale reassembly msgId: ", rit->first,
						" from node: ", session.remote);
						rit = cs.reassembly.erase(rit);
					}
					else
					{
						++rit;
					}
				}
			}

			it++;
		}
	}

	Session& GetOrCreateSession(const NodeId remote)
	{
		auto it = s_sessions.find(remote);
		if (it != s_sessions.end())
		{
			return it->second;
		}

		Session fresh;
		fresh.remote = remote;
		fresh.localIsn = RandomIsn();
		fresh.state = State::SYN_SENT;
		fresh.lastRecvTime = Clock::now();
		fresh.lastSendTime = Clock::now();
		fresh.synSentTime = Clock::now();

		auto [insertedIt, _] = s_sessions.emplace(remote, std::move(fresh));

		SendSyn(insertedIt->second);

		return insertedIt->second;
	}

	void EnqueueMessage(Session& session, const ChannelId channel, const std::byte* data, const u32 size)
	{
		auto& cs = GetChannel(session, channel);
		const auto* bytes = reinterpret_cast<const u8*>(data);

		if (size <= MSS)
		{
			if (cs.pending.size() >= MAX_PENDING_MESSAGES)
			{
				return;
			}

			PendingPacket msg;
			msg.msgId = session.nextMsgId++;
			msg.fragIdx = 0;
			msg.fragCount = 1;
			msg.payload.assign(bytes, bytes + size);
			cs.pending.push_back(std::move(msg));
			return;
		}

		const u32 fragCount = (size + MSS - 1) / MSS;
		const u32 msgId = session.nextMsgId++;

		for (u32 i = 0; i < fragCount; ++i)
		{
			const u32 offset = i * MSS;
			const u32 length = std::min(MSS, size - offset);

			if (cs.pending.size() >= MAX_PENDING_MESSAGES)
			{
				return;
			}

			PendingPacket msg;
			msg.msgId = msgId;
			msg.fragIdx = static_cast<u16>(i);
			msg.fragCount = static_cast<u16>(fragCount);
			msg.payload.assign(bytes + offset, bytes + offset + length);
			cs.pending.push_back(std::move(msg));
		}
	}

	bool SendReliable(const NodeId nodeId, const std::byte* data, const u32 size, const ChannelId channel)
	{
		Session& session = GetOrCreateSession(nodeId);
		EnqueueMessage(session, channel, data, size);

		if (session.state == State::ESTABLISHED)
		{
			FlushPending(session, channel);
		}

		return true;
	}

	bool SendRaw(const NodeId nodeId, const std::byte* data, const u32 size)
	{
		const auto bytes = SerializeRaw(data, size);
		return Node::Send(nodeId, bytes, false);
	}

	std::optional<NodeId> ResolveName(const std::string& name)
	{
		for (const auto& [node, _] : Node::GetRoutes())
		{
			if (node.name == name)
			{
				return node.id;
			}
		}

		return std::nullopt;
	}
}

bool Transport::Init(const u16 port, const std::string& name)
{
	if (!Node::Init(port, name))
	{
		Logger::Write<LogLevel::ERROR>("Transport failed to init node on port: ", port);
		return false;
	}

	s_selfId = Node::GetSelfId();
	s_sessions.clear();

	Logger::Write<LogLevel::INFO>("Transport initialized with node id: ", s_selfId, " name: ", name,
	" port: ", Networking::GetLocalPort());
	return true;
}

void Transport::Shutdown()
{
	Logger::Write<LogLevel::INFO>("Transport shutting down node: ", s_selfId);

	for (auto& [remote, session] : s_sessions)
	{
		if (session.state == State::ESTABLISHED)
		{
			SendFin(session);
		}
	}

	s_sessions.clear();
	s_selfId = 0;

	Node::Shutdown();
}

std::optional<Transport::PeerId> Transport::Connect(const Networking::Address& address)
{
	const auto peerId = Node::Connect(address);
	if (!peerId)
	{
		Logger::Write<LogLevel::WARN>("Transport failed to connect to: ", address.ip, ":", address.port);
	}

	return peerId;
}

std::optional<Transport::PeerId> Transport::Connect(const std::string& ip, const u16 port)
{
	return Connect({ip, port});
}

void Transport::Disconnect(const PeerId peerId)
{
	Logger::Write<LogLevel::DEBUG>("Transport disconnecting peer: ", peerId);
	Node::Disconnect(peerId);
}

void Transport::Poll(std::queue<Event>& events, const u32 timeoutMs)
{
	std::queue<Networking::Event> nodeEvents;
	Node::Poll(nodeEvents, timeoutMs);

	while (!nodeEvents.empty())
	{
		auto& ev = nodeEvents.front();

		if (ev.type == Networking::EventType::RECEIVE && !ev.data.empty())
		{
			const auto tag = static_cast<u8>(ev.data[0]);

			if (tag == TAG_TRANSPORT)
			{
				HandleTransportPacket(events, ev.data, ev.peer.id);
			}
			else if (tag == TAG_RAW)
			{
				HandleRawPacket(events, ev.data);
			}
			else
			{
				Logger::Write<LogLevel::WARN>("Transport dropping unknown packet tag: ", tag);
			}
		}

		nodeEvents.pop();
	}

	UpdateTimers(events);
}

bool Transport::Send(const NodeId nodeId, const std::byte* data, const u32 size, const ChannelId channel,
const bool reliable)
{
	if (nodeId == 0 || nodeId == s_selfId)
	{
		Logger::Write<LogLevel::WARN>("Transport refusing send to invalid node: ", nodeId);
		return false;
	}

	if (!reliable)
	{
		return SendRaw(nodeId, data, size);
	}

	return SendReliable(nodeId, data, size, channel);
}

bool Transport::Send(const NodeId nodeId, const std::vector<std::byte>& data, const ChannelId channel,
const bool reliable)
{
	return Send(nodeId, data.data(), static_cast<u32>(data.size()), channel, reliable);
}

bool Transport::Send(const std::string& name, const std::byte* data, const u32 size, const ChannelId channel,
const bool reliable)
{
	if (name.empty())
	{
		Logger::Write<LogLevel::WARN>("Transport refusing send to empty name");
		return false;
	}

	const auto nodeId = ResolveName(name);
	if (!nodeId)
	{
		Logger::Write<LogLevel::WARN>("Transport could not resolve name: ", name);
		return false;
	}

	return Send(*nodeId, data, size, channel, reliable);
}

bool Transport::Send(const std::string& name, const std::vector<std::byte>& data, const ChannelId channel,
const bool reliable)
{
	return Send(name, data.data(), static_cast<u32>(data.size()), channel, reliable);
}

void Transport::Close(const NodeId nodeId)
{
	auto it = s_sessions.find(nodeId);
	if (it == s_sessions.end())
	{
		return;
	}

	Session& session = it->second;

	if (session.state == State::ESTABLISHED)
	{
		SendFin(session);
		session.state = State::FIN_WAIT;
		return;
	}

	if (session.state == State::SYN_SENT || session.state == State::SYN_RECEIVED)
	{
		Logger::Write<LogLevel::INFO>("Transport aborting handshake with node: ", nodeId);
		SendRst(session);
		s_sessions.erase(it);
		return;
	}

	s_sessions.erase(it);
}

Transport::NodeId Transport::GetSelfId()
{
	return s_selfId;
}

std::vector<Transport::SessionInfo> Transport::GetSessions()
{
	std::vector<SessionInfo> result;
	result.reserve(s_sessions.size());

	for (const auto& [remote, session] : s_sessions)
	{
		SessionInfo info;
		info.nodeId = remote;
		info.rttMs = static_cast<u32>(session.srtt);
		info.cwnd = static_cast<u32>(session.cwnd);
		info.ssthresh = static_cast<u32>(session.ssthresh);

		u32 unacked = 0;
		u32 pending = 0;

		for (const auto& [_, cs] : session.channels)
		{
			unacked += static_cast<u32>(cs.unacked.size());
			pending += static_cast<u32>(cs.pending.size());
		}

		info.unacked = unacked;
		info.pending = pending;

		for (const auto& [node, _] : Node::GetRoutes())
		{
			if (node.id == remote)
			{
				info.name = node.name;
				break;
			}
		}

		result.push_back(std::move(info));
	}

	return result;
}

std::vector<Node::Node> Transport::GetNodes()
{
	std::vector<Node::Node> result;

	for (const auto& [node, _] : Node::GetRoutes())
	{
		if (node.id == s_selfId)
		{
			continue;
		}

		result.push_back(node);
	}

	return result;
}