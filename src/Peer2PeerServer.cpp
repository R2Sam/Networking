#include "Peer2PeerServer.h"

#include "Assert.h"

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/utility.hpp>
#include "Peer2PeerPacket.h"
#include "cereal/MyCereal.h"

#include <cstring>

Peer2PeerServer::Peer2PeerServer() 
{
	_server.InitServer(1313, 100);
}

void Peer2PeerServer::Poll(const u32 timeoutMs) 
{
	std::queue<NetworkEvent> events;

	_server.Poll(events, timeoutMs);

	while(events.size())
	{
		NetworkEvent& event = events.front();

		switch (event.type)
		{
			case NetworkEventType::Connect:
			{
				u32 data;
				Assert(event.data.size() == sizeof(data), "memcpy size must match");
				memcpy(&data, event.data.data(), sizeof(data));

				if (data)
				{
					HandleHost(event.peer);
				}
			}
			break;

			case NetworkEventType::Disconnect:
			{
				_hosts.erase(event.peer);
			}
			break;

			case NetworkEventType::Receive:
			{
				Peer2PeerPacket packet = Deserialize<Peer2PeerPacket>(event.data);

				HandleReceive(event.peer, packet);
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
	_hosts.emplace(peer);

	Peer2PeerPacket packet{Peer2PeerPacketType::HostId, peer};

	_server.Send(peer, 1, Serialize<Peer2PeerPacket>(packet));
}

void Peer2PeerServer::HandleReceive(const PeerId peer, const Peer2PeerPacket& packet) 
{
	if (packet.type == Peer2PeerPacketType::Request)
	{
		HandleRequest(peer, packet.peer);
	}

	else if (packet.type == Peer2PeerPacketType::Hosts)
	{
		HandleList(peer);
	}
}

void Peer2PeerServer::HandleRequest(const PeerId peer, const PeerId target) 
{
	if (_hosts.find(target) == _hosts.end())
	{
		Peer2PeerPacket packet{Peer2PeerPacketType::Address, 0, Address()};

		_server.Send(peer, 1, Serialize<Peer2PeerPacket>(packet));
		return;
	}

	Peer2PeerPacket hostPacket{Peer2PeerPacketType::Address, 0, _server.GetPeer(target).address};
	std::vector<u8> hostData = Serialize<Peer2PeerPacket>(hostPacket);


	Peer2PeerPacket clientPacket{Peer2PeerPacketType::Address, 0, _server.GetPeer(peer).address};
	std::vector<u8> clientData = Serialize<Peer2PeerPacket>(clientPacket);

	_server.Send(peer, 1, hostData);
	_server.Send(target, 1, clientData);
}

void Peer2PeerServer::HandleList(const PeerId peer) 
{
	std::vector<std::pair<PeerId, Address>> hosts;

	for (const PeerId id : _hosts)
	{
		hosts.emplace_back(id, _server.GetPeer(id).address);
	}

	Peer2PeerPacket packet{Peer2PeerPacketType::Hosts, 0, Address(), hosts};

	_server.Send(peer, 1, Serialize<Peer2PeerPacket>(packet));
}