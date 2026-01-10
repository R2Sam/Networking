#include "PeerManager.h"

#include "enet/enet.h"

#include "Assert.h"

Peer PeerManager::AddPeer(_ENetPeer* enetPeer, const Address& address, const ConnectionState state) 
{
	PeerId id = AllocateId();

	Peer peer;
	peer.id = id;
	peer.enetPeer = enetPeer;
	peer.address = address;
	peer.state = state;

	_peers.emplace(id, peer);

	Assert(enetPeer, "Cannot pass an invalid enetPeer");

	_ids.emplace(enetPeer->connectID, id);

	return peer;
}

void PeerManager::RemovePeer(const PeerId peerId) 
{
	if (peerId == 0)
	{
		return;
	}

	auto it = _peers.find(peerId);
	if (it != _peers.end())
	{
		Peer p = it->second;

		_ids.erase(p.enetPeer->connectID);
	}

	else
	{
		u32 enetId = 0;
		for (const auto pair : _ids)
		{
			if (pair.second == peerId)
			{
				enetId = pair.first;
			}
		}

		_ids.erase(enetId);
	}

	FreeId(peerId);
	_peers.erase(it);
}

Peer PeerManager::GetPeer(const PeerId peerId) const
{
	auto it = _peers.find(peerId);
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

Peer PeerManager::GetPeerEnet(const u32 enetPeerId) const
{
	auto it = _ids.find(enetPeerId);
	if (it != _ids.end())
	{
		return GetPeer(it->second);
	}

	else
	{
		Peer p;
		return p;
	}
}

const std::unordered_map<PeerId, Peer>& PeerManager::GetPeers() const 
{
	return _peers;
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

void PeerManager::FreeId(const PeerId peerId) 
{
	if (peerId == 0)
	{
		return;
	}

	_freeIds.push(peerId);
}