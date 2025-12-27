#include "PeerManager.h"

#include "enet/enet.h"

#include "Assert.h"

PeerId PeerManager::AddPeer(_ENetPeer* enetPeer, const Address& address, const ConnectionState state) 
{
	PeerId id = AllocateId();

	Peer p;
	p.id = id;
	p.enetPeer = enetPeer;
	p.address = address;
	p.state = state;

	_peers.emplace(id, std::move(p));

	Assert(enetPeer, "Cannot pass an invalid enetPeer");

	_enetIds.emplace(enetPeer->connectID, id);

	_sendQueues.emplace(id, std::queue<std::vector<u8>>());
	_receiveBuffers.emplace(id, std::vector<u8>());

	return id;
}

PeerId PeerManager::AddPeer(const i32 socket, const Address& address, const ConnectionState state) 
{
	PeerId id = AllocateId();

	Peer p;
	p.id = id;
	p.socket = socket;
	p.address = address;
	p.state = state;

	_peers.emplace(id, std::move(p));

	Assert(socket >= 0, "Cannot pass an invalid socket");

	_socketIds.emplace(socket, id);

	_sendQueues.emplace(id, std::queue<std::vector<u8>>());
	_receiveBuffers.emplace(id, std::vector<u8>());

	return id;
}

void PeerManager::RemovePeer(const PeerId peer) 
{
	if (peer == 0)
	{
		return;
	}

	auto it = _peers.find(peer);
	if (it != _peers.end())
	{
		Peer p = it->second;

		_enetIds.erase(p.enetPeer->connectID);
		_socketIds.erase(p.socket);
	}

	else
	{
		for (const auto pair : _enetIds)
		{
			if (pair.second == peer)
			{
				_enetIds.erase(pair.first);
				break;
			}
		}

		for (const auto pair : _socketIds)
		{
			if (pair.second == peer)
			{
				_socketIds.erase(pair.first);
				break;
			}
		}
	}

	_sendQueues.erase(peer);
	_receiveBuffers.erase(peer);

	FreeId(peer);
	_peers.erase(it);
}

bool PeerManager::UpdatePeer(const Peer peer) 
{
	auto it = _peers.find(peer.id);
	if (it == _peers.end())
	{
		return false;
	}

	it->second = peer;

	return true;
}

Peer PeerManager::GetPeer(const PeerId peer) 
{
	auto it = _peers.find(peer);
	if (it != _peers.end())
	{
		return it->second;
	}

	else
	{
		Peer p;
		return p;
	}
}

Peer PeerManager::GetPeerEnet(const u32 enetPeerId) 
{
	auto it = _enetIds.find(enetPeerId);
	if (it != _enetIds.end())
	{
		return GetPeer(it->second);
	}

	else
	{
		Peer p;
		return p;
	}
}

Peer PeerManager::GetPeerSocket(const i32 socket) 
{
	auto it = _socketIds.find(socket);
	if (it != _socketIds.end())
	{
		return GetPeer(it->second);
	}

	else
	{
		Peer p;
		return p;
	}
}

std::vector<PeerId> PeerManager::GetAllPeerIds() 
{
	std::vector<PeerId> peers;

	for (const auto& [peerId, peer] : _peers)
	{
		peers.push_back(peerId);
	}

	return peers;
}

std::queue<std::vector<u8>>& PeerManager::GetSendQueue(const PeerId peer) 
{
	Assert(_peers.find(peer) != _peers.end(), "Peer must exist to access send queue");

	return _sendQueues[peer];
}

std::vector<u8>& PeerManager::GetRecieveBuffer(const PeerId peer) 
{
	Assert(_peers.find(peer) != _peers.end(), "Peer must exist to access recieve buffer");

	return _receiveBuffers[peer];
}

PeerId PeerManager::AllocateId() 
{
	if (_freeIds.size())
	{
		PeerId id = _freeIds.back();
		_freeIds.pop();

		return id;
	}

	return _nextId++;
}

void PeerManager::FreeId(const PeerId peer) 
{
	if (peer == 0)
	{
		return;
	}

	_freeIds.push(peer);
}