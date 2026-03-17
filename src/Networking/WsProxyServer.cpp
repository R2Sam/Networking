#include "WsProxyServer.hpp"

#include <cstring>
#include <iterator>

WsProxyServer::WsProxyServer(const u16 port)
{
	m_enetCore.InitServer(port, 8);
	m_wsCore.InitServer(port);
}

void WsProxyServer::Update()
{
	std::queue<NetworkEvent> enetEvents;
	std::queue<NetworkEvent> wsEvents;

	m_enetCore.Poll(enetEvents, 5);
	m_wsCore.Poll(wsEvents, 5);

	HandleEnet(enetEvents);
	HandleWs(wsEvents);
}

void WsProxyServer::HandleEnet(std::queue<NetworkEvent>& events)
{
	while (!events.empty())
	{
		NetworkEvent& event = events.front();

		switch (event.type)
		{
		case NetworkEventType::CONNECT:
		{
			m_enetPeers.emplace(event.peer.id, "");
		}
		break;

		case NetworkEventType::DISCONNECT:
		{
			for (auto& pair : m_wsPeers)
			{
				if (pair.second == event.peer.id)
				{
					m_wsCore.Disconnect(pair.first);
				}
			}

			m_enetPeers.erase(event.peer.id);
		}
		break;

		case NetworkEventType::RECEIVE:
		{
			if (m_enetPeers[event.peer.id].empty())
			{
				m_enetPeers[event.peer.id].assign(event.data.begin(), event.data.end());
				break;
			}

			if (event.data.size() < sizeof(PeerId))
			{
				break;
			}

			// First bytes are the target wsPeer
			PeerId targetPeer;
			memcpy(&targetPeer, event.data.data(), sizeof(targetPeer));

			// Make sure that the peer exists
			// and sure that the peer is even interested in us
			auto it = m_wsPeers.find(targetPeer);
			if (it == m_wsPeers.end() || it->second != event.peer.id)
			{

				m_enetCore.Send(event.peer.id, event.data.data(), sizeof(targetPeer));
				break;
			}

			m_wsCore.Send(targetPeer, event.data.data() + sizeof(targetPeer), event.data.size() - sizeof(targetPeer));
		}
		break;
		default:
			break;
		}

		events.pop();
	}
}

void WsProxyServer::HandleWs(std::queue<NetworkEvent>& events)
{
	while (!events.empty())
	{
		NetworkEvent& event = events.front();

		switch (event.type)
		{
		case NetworkEventType::CONNECT:
		{
			m_wsPeers.emplace(event.peer.id, 0);
		}
		break;

		case NetworkEventType::DISCONNECT:
		{
			if (m_wsPeers[event.peer.id])
			{
				m_enetCore.Send(m_wsPeers[event.peer.id], (u8*)&event.peer.id, sizeof(event.peer.id));
			}

			m_wsPeers.erase(event.peer.id);
		}
		break;

		case NetworkEventType::RECEIVE:
		{
			// If it doesn't have a pair look for it
			if (!m_wsPeers[event.peer.id])
			{
				for (const auto& pair : m_enetPeers)
				{
					if (pair.second == std::string(event.data.begin(), event.data.end()))
					{
						m_wsPeers[event.peer.id] = pair.first;
						break;
					}
				}

				// If no pair found
				if (!m_wsPeers[event.peer.id])
				{
					m_wsCore.Disconnect(event.peer.id);
				}

				break;
			}

			// Make sure it's pair is still connected
			auto it = m_enetPeers.find(m_wsPeers[event.peer.id]);
			if (it == m_enetPeers.end())
			{
				m_wsCore.Disconnect(event.peer.id);
				break;
			}

			std::vector<u8> data(sizeof(event.peer.id));
			memcpy(data.data(), &event.peer.id, sizeof(event.peer.id));
			data.insert(data.end(), std::move_iterator(event.data.begin()), std::move_iterator(event.data.end()));

			m_enetCore.Send(m_wsPeers[event.peer.id], std::move(data));
		}
		break;

		default:
			break;
		}

		events.pop();
	}
}