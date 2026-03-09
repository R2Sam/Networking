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

	m_peers.emplace(id, peer);

	Assert(enetPeer, "Cannot pass an invalid enetPeer");

	m_enetIds.emplace(enetPeer->connectID, id);

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

	m_peers.emplace(id, std::move(peer));

	m_wsIds.emplace(wsPeer, id);

	return peer;
}

bool PeerManager::EditPeer(const Peer& peer)
{
	if (peer.id == 0)
	{
		return false;
	}

	auto it = m_peers.find(peer.id);
	if (it == m_peers.end())
	{
		return false;
	}

	it->second = peer;

	return true;
}

void PeerManager::RemovePeer(const PeerId peerId)
{
	if (peerId == 0)
	{
		return;
	}

	auto it = m_peers.find(peerId);
	if (it != m_peers.end())
	{
		Peer peer = it->second;

		if (peer.enetPeer)
		{
			m_enetIds.erase(peer.enetPeer->connectID);
		}
		m_wsIds.erase(peer.wsPeer);
	}

	else
	{
		u32 enetId = 0;
		for (const auto pair : m_enetIds)
		{
			if (pair.second == peerId)
			{
				enetId = pair.first;
			}
		}

		m_enetIds.erase(enetId);

		ws_cli_conn_t wsId = 0;
		for (const auto pair : m_wsIds)
		{
			if (pair.second == peerId)
			{
				wsId = pair.first;
			}
		}

		m_wsIds.erase(wsId);
	}

	m_peers.erase(it);
	FreeId(peerId);
}

Peer PeerManager::GetPeer(const PeerId peerId) const
{
	auto it = m_peers.find(peerId);
	if (it != m_peers.end())
	{
		return it->second;
	}

	Peer p;
	return p;
}

Peer PeerManager::GetPeerEnet(const u32 enetPeerId) const
{
	auto it = m_enetIds.find(enetPeerId);
	if (it != m_enetIds.end())
	{
		return GetPeer(it->second);
	}

	Peer p;
	return p;
}

Peer PeerManager::GetPeerWs(const ws_cli_conn_t wsPeer) const
{
	auto it = m_wsIds.find(wsPeer);
	if (it != m_wsIds.end())
	{
		return GetPeer(it->second);
	}

	Peer peer;
	return peer;
}

std::unordered_map<PeerId, Peer> PeerManager::GetPeers() const
{
	return m_peers;
}

PeerId PeerManager::AllocateId()
{
	if (!m_freeIds.empty())
	{
		PeerId id = m_freeIds.back();
		m_freeIds.pop();

		return id;
	}

	return m_nextId++;
}

void PeerManager::FreeId(const PeerId peerId)
{
	if (peerId == 0)
	{
		return;
	}

	m_freeIds.push(peerId);
}