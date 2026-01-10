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

	_enetIds.emplace(enetPeer->connectID, id);

	return peer;
}

Peer PeerManager::AddPeer(const ws_cli_conn_t wsPeer, const Address& address, const ConnectionState state) 
{
	PeerId id = AllocateId();

	Peer peer;
	peer.id = id;
	peer.wsPeer = wsPeer;
	peer.address = address;
	peer.state = state;

	_peers.emplace(id, std::move(peer));

	_wsIds.emplace(wsPeer, id);

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
		Peer peer = it->second;

		_enetIds.erase(peer.enetPeer->connectID);
		_wsIds.erase(peer.wsPeer);
	}

	else
	{
		u32 enetId = 0;
		for (const auto pair : _enetIds)
		{
			if (pair.second == peerId)
			{
				enetId = pair.first;
			}
		}

		_enetIds.erase(enetId);

		ws_cli_conn_t wsId = 0;
		for (const auto pair : _wsIds)
		{
			if (pair.second == peerId)
			{
				wsId = pair.first;
			}
		}

		_wsIds.erase(wsId);
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

Peer PeerManager::GetPeerWs(const ws_cli_conn_t wsPeer) const
{
	auto it = _wsIds.find(wsPeer);
	if (it != _wsIds.end())
	{
		return GetPeer(it->second);
	}

	else
	{
		Peer peer;
		return peer;
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