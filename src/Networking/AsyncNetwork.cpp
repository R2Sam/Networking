#include "AsyncNetwork.hpp"
#include "Networking/NetworkTypes.hpp"
#include <thread>

AsyncNetwork::AsyncNetwork()
{
}

AsyncNetwork::~AsyncNetwork()
{
	Stop();
}

void AsyncNetwork::ShutDown()
{
	Stop();
	m_core.Shutdown();
}

bool AsyncNetwork::InitServer(const u16 port, const u32 maxPeers, const u32 channels)
{
	return m_core.InitServer(port, maxPeers, channels);
}

bool AsyncNetwork::InitClient(const u32 maxPeers, const u32 channels)
{
	return m_core.InitClient(maxPeers, channels);
}

void AsyncNetwork::Start()
{
	if (m_running)
	{
		return;
	}

	m_running = true;
	m_thread = std::jthread(&AsyncNetwork::Loop, this);
}

void AsyncNetwork::Stop()
{
	if (!m_running)
	{
		return;
	}

	m_running = false;

	if (m_thread.joinable())
	{
		m_thread.join();
	}
}

void AsyncNetwork::Connect(const Address& address, const u32 data)
{
	m_commandQueue.enqueue({CommandType::CONNECT, 0, address, data});
}

void AsyncNetwork::Disconnect(const PeerId peerId, const u32 data)
{
	m_commandQueue.enqueue({CommandType::DISCONNECT, peerId, {}, data});
}

void AsyncNetwork::Send(const PeerId peerId, std::vector<u8>&& data, const ChannelId channel, const bool reliable)
{
	m_commandQueue.enqueue({CommandType::SEND, peerId, {}, 0, std::move(data), channel, reliable});
}

void AsyncNetwork::Send(const PeerId peerId, const u8* data, const u32 size, const ChannelId channel,
const bool reliable)
{
	std::vector<u8> vec(data, data + size);
	Send(peerId, std::move(vec), channel, reliable);
}

bool AsyncNetwork::PopEvent(NetworkEvent& event)
{
	return m_eventQueue.try_dequeue(event);
}

void AsyncNetwork::Loop()
{
	const u32 timeoutMs = 10;

	std::queue<NetworkEvent> events;

	while (m_running)
	{
		m_core.Poll(events, timeoutMs);
		while (!events.empty())
		{
			m_eventQueue.enqueue(events.front());
			events.pop();
		}

		Command command;
		while (m_commandQueue.try_dequeue(command))
		{
			switch (command.type)
			{
			case CommandType::CONNECT:
				m_core.Connect(command.address, command.extraData);
				break;
			case CommandType::DISCONNECT:
				m_core.Disconnect(command.peerId, command.extraData);
				break;
			case CommandType::SEND:
				m_core.Send(command.peerId, std::move(command.data), command.channel, command.reliable);
				break;
			}
		}
	}
}