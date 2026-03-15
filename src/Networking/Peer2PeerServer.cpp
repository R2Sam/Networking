#include "Peer2PeerServer.h"

#include "Assert.h"

#include "Peer2PeerPacket.h"
#include "cereal/MyCereal.h"
#include <cereal/types/string.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>

#include <cstring>

Peer2PeerServer::Peer2PeerServer()
{
	m_server.InitServer(1313, 100);
}

void Peer2PeerServer::Poll(const u32 timeoutMs)
{
	std::queue<NetworkEvent> events;

	m_server.Poll(events, timeoutMs);

	while (!events.empty())
	{
		NetworkEvent& event = events.front();

		switch (event.type)
		{
		case NetworkEventType::CONNECT:
		{
			u32 data;
			Assert(event.data.size() == sizeof(data), "memcpy size must match");
			memcpy(&data, event.data.data(), sizeof(data));

			if (data)
			{
				HandleHost(event.peer.id);
			}
		}
		break;

		case NetworkEventType::DISCONNECT:
		{
			m_hosts.erase(event.peer.id);
		}
		break;

		case NetworkEventType::RECEIVE:
		{
			Peer2PeerPacket packet = Deserialize<Peer2PeerPacket>(event.data);

			HandleReceive(event.peer.id, packet);
		}
		break;

		default:
			break;
		}

		events.pop();
	}
}

void Peer2PeerServer::HandleHost(const PeerId peer)
{
	m_hosts.emplace(peer);

	Peer2PeerPacket packet{Peer2PeerPacketType::HOST_ID, peer};

	m_server.Send(peer, Serialize<Peer2PeerPacket>(packet));
}

void Peer2PeerServer::HandleReceive(const PeerId peer, const Peer2PeerPacket& packet)
{
	if (packet.type == Peer2PeerPacketType::REQUEST)
	{
		HandleRequest(peer, packet.peer);
	}

	else if (packet.type == Peer2PeerPacketType::HOSTS)
	{
		HandleList(peer);
	}
}

void Peer2PeerServer::HandleRequest(const PeerId peer, const PeerId target)
{
	if (m_hosts.find(target) == m_hosts.end())
	{
		Peer2PeerPacket packet{Peer2PeerPacketType::ADDRESS, 0, Address()};

		m_server.Send(peer, Serialize<Peer2PeerPacket>(packet));
		return;
	}

	Peer2PeerPacket hostPacket{Peer2PeerPacketType::ADDRESS, 0, m_server.GetPeer(target).address};
	std::vector<u8> hostData = Serialize<Peer2PeerPacket>(hostPacket);

	Peer2PeerPacket clientPacket{Peer2PeerPacketType::ADDRESS, 0, m_server.GetPeer(peer).address};
	std::vector<u8> clientData = Serialize<Peer2PeerPacket>(clientPacket);

	m_server.Send(peer, std::move(hostData));
	m_server.Send(target, std::move(clientData));
}

void Peer2PeerServer::HandleList(const PeerId peer)
{
	std::vector<std::pair<PeerId, Address>> hosts;

	hosts.reserve(m_hosts.size());
	for (const PeerId id : m_hosts)
	{
		hosts.emplace_back(id, m_server.GetPeer(id).address);
	}

	Peer2PeerPacket packet{Peer2PeerPacketType::HOSTS, 0, Address(), hosts};

	m_server.Send(peer, Serialize<Peer2PeerPacket>(packet));
}