#pragma once

#include "NetworkCore.hpp"
#include "Networking/NetworkTypes.hpp"
#include "Types.hpp"

#ifndef assert
#define assert(condition) Assert(condition)
#endif

#include "ThreadSafeQueue/concurrentqueue.h"
#include <thread>
#include <vector>

class AsyncNetwork
{
public:

	AsyncNetwork();
	~AsyncNetwork();

	bool InitServer(const u16 port, const u32 maxPeers = 64, const u32 channels = 1);
	bool InitClient(const u32 maxPeers = 64, const u32 channels = 1);
	void ShutDown();

	void Start();
	void Stop();

	void Connect(const Address& address, const u32 data = 0);
	void Disconnect(const PeerId peerId, const u32 data = 0);

	void Send(const PeerId peerId, std::vector<u8>&& data, const ChannelId channel = 0, const bool reliable = true);
	void Send(const PeerId peerId, const u8* data, const u32 size, const ChannelId channel = 0,
	const bool reliable = true);

	bool PopEvent(NetworkEvent& event);

private:

	enum class CommandType : u8
	{
		CONNECT,
		DISCONNECT,
		SEND
	};

	struct Command
	{
		CommandType type;
		PeerId peerId = 0;
		Address address;
		u32 extraData = 0;
		std::vector<u8> data;
		ChannelId channel = 0;
		bool reliable = true;
	};

	void Loop();

	NetworkCore m_core;
	std::thread m_thread;
	moodycamel::ConcurrentQueue<Command> m_commandQueue;
	moodycamel::ConcurrentQueue<NetworkEvent> m_eventQueue;
	std::atomic<bool> m_running = false;
};