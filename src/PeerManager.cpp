#include "PeerManager.h"

#ifndef __EMSCRIPTEN__
#include "enet/enet.h"
#else
#include "enet/enet-Web.h"
#endif

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

	_ids.emplace(enetPeer->connectID, id);

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

		_ids.erase(p.enetPeer->connectID);
	}

	else
	{
		u32 enetId = 0;
		for (const auto pair : _ids)
		{
			if (pair.second == peer)
			{
				enetId = pair.first;
			}
		}

		_ids.erase(enetId);
	}

	FreeId(peer);
	_peers.erase(it);
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