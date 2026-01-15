#include "WsProxyServer.h"

#include <cstring>
#include <iterator>

WsProxyServer::WsProxyServer(const u16 port)
{
	_enetCore.InitServer(port, 8);
	_wsCore.InitServer(port);
}

void WsProxyServer::Update()
{
	std::queue<NetworkEvent> enetEvents;
	std::queue<NetworkEvent> wsEvents;

	_enetCore.Poll(enetEvents, 5);
	_wsCore.Poll(wsEvents, 5);

	HandleEnet(enetEvents);
	HandleWs(wsEvents);
}

void WsProxyServer::HandleEnet(std::queue<NetworkEvent>& events)
{
	while (events.size())
	{
		NetworkEvent& event = events.front();

		switch (event.type)
		{
		case NetworkEventType::Connect:
		{
			_enetPeers.emplace(event.peer.id, "");
		}
		break;

		case NetworkEventType::Disconnect:
		{
			for (auto& pair : _wsPeers)
			{
				if (pair.second == event.peer.id)
				{
					_wsCore.Disconnect(pair.first);
				}
			}

			_enetPeers.erase(event.peer.id);
		}
		break;

		case NetworkEventType::Receive:
		{
			if (_enetPeers[event.peer.id].empty())
			{
				_enetPeers[event.peer.id].assign(event.data.begin(), event.data.end());
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
			auto it = _wsPeers.find(targetPeer);
			if (it == _wsPeers.end() || it->second != event.peer.id)
			{

				_enetCore.Send(event.peer.id, event.data.data(), sizeof(targetPeer));
				break;
			}

			_wsCore.Send(targetPeer, event.data.data() + sizeof(targetPeer), event.data.size() - sizeof(targetPeer));
		}
		break;
		}

		events.pop();
	}
}

void WsProxyServer::HandleWs(std::queue<NetworkEvent>& events)
{
	while (events.size())
	{
		NetworkEvent& event = events.front();

		switch (event.type)
		{
		case NetworkEventType::Connect:
		{
			_wsPeers.emplace(event.peer.id, 0);
		}
		break;

		case NetworkEventType::Disconnect:
		{
			if (_wsPeers[event.peer.id])
			{
				_enetCore.Send(_wsPeers[event.peer.id], (u8*)&event.peer.id, sizeof(event.peer.id));
			}

			_wsPeers.erase(event.peer.id);
		}
		break;

		case NetworkEventType::Receive:
		{
			// If it doesn't have a pair look for it
			if (!_wsPeers[event.peer.id])
			{
				for (const auto& pair : _enetPeers)
				{
					if (pair.second == std::string(event.data.begin(), event.data.end()))
					{
						_wsPeers[event.peer.id] = pair.first;
						break;
					}
				}

				// If no pair found
				if (!_wsPeers[event.peer.id])
				{
					_wsCore.Disconnect(event.peer.id);
				}

				break;
			}

			// Make sure it's pair is still connected
			auto it = _enetPeers.find(_wsPeers[event.peer.id]);
			if (it == _enetPeers.end())
			{
				_wsCore.Disconnect(event.peer.id);
				break;
			}

			std::vector<u8> data(sizeof(event.peer.id));
			memcpy(data.data(), &event.peer.id, sizeof(event.peer.id));
			data.insert(data.end(), std::move_iterator(event.data.begin()), std::move_iterator(event.data.end()));

			_enetCore.Send(_wsPeers[event.peer.id], data);
		}
		break;
		}

		events.pop();
	}
}