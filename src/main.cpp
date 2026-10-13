#include "Log/Logger.hpp"
#include "Networking/Transport.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	std::mutex s_cmdMutex;
	std::queue<std::string> s_commands;
	std::atomic<bool> s_running{true};

	bool IsNumeric(const std::string& s)
	{
		if (s.empty())
		{
			return false;
		}

		return std::all_of(s.begin(), s.end(), [](unsigned char c)
		{
			return std::isdigit(c) != 0;
		});
	}

	void InputThread()
	{
		std::string line;

		while (s_running && std::getline(std::cin, line))
		{
			std::lock_guard<std::mutex> lock(s_cmdMutex);
			s_commands.push(line);
		}

		s_running = false;
	}

	std::vector<std::byte> ToBytes(const std::string& text)
	{
		std::vector<std::byte> data(text.size());

		if (!text.empty())
		{
			std::memcpy(data.data(), text.data(), text.size());
		}

		return data;
	}

	std::string FromBytes(const std::vector<std::byte>& data)
	{
		if (data.empty())
		{
			return {};
		}

		return std::string(reinterpret_cast<const char*>(data.data()), data.size());
	}

	std::optional<Transport::NodeId> ResolveTarget(const std::string& target)
	{
		if (IsNumeric(target))
		{
			return static_cast<Transport::NodeId>(std::stoull(target));
		}

		for (const auto& node : Transport::GetNodes())
		{
			if (node.name == target)
			{
				return node.id;
			}
		}

		return std::nullopt;
	}

	void PrintHelp()
	{
		Logger::Print("Commands:\n"
					  "  connect <ip>:<port>              - connect to a peer\n"
					  "  disconnect <peerId>              - disconnect a direct peer\n"
					  "  send <nodeId|name> <text>        - reliable send on channel 0\n"
					  "  sendc <nodeId|name> <chan> <txt> - reliable send on a specific channel\n"
					  "  raw <nodeId|name> <text>         - unreliable raw send\n"
					  "  big <nodeId|name> <bytes>        - send a large buffer to test fragmentation\n"
					  "  stress <nodeId|name> <n> <size>  - send n messages of size bytes\n"
					  "  close <nodeId>                   - gracefully close a transport session\n"
					  "  sessions                         - list transport sessions\n"
					  "  peers                            - list direct peers\n"
					  "  nodes                            - list known nodes\n"
					  "  help                             - show this help\n"
					  "  quit                             - exit\n");
	}

	void CmdSessions()
	{
		const auto sessions = Transport::GetSessions();

		Logger::Print("Sessions (", sessions.size(), "):\n");

		for (const auto& s : sessions)
		{
			Logger::Print(" | Node: ", s.nodeId, " | Name: ", s.name.empty() ? "-" : s.name, " | RTT: ", s.rttMs, "ms",
			" | cwnd: ", s.cwnd, " | ssthresh: ", s.ssthresh, " | unacked: ", s.unacked, " | pending: ", s.pending,
			"\n");
		}
	}

	void CmdNodes()
	{
		const auto nodes = Transport::GetNodes();

		Logger::Print("Nodes (", nodes.size(), "):\n");

		for (const auto& n : nodes)
		{
			Logger::Print(" | Node: ", n.id, " | Name: ", n.name.empty() ? "-" : n.name, "\n");
		}
	}

	void CmdPeers()
	{
		const auto ids = Networking::GetPeerIds();

		Logger::Print("Peers (", ids.size(), "):\n");

		for (const auto id : ids)
		{
			const auto peer = Networking::GetPeer(id);
			if (!peer)
			{
				continue;
			}

			Logger::Print(" | Peer: ", peer->id, " | Addr: ", peer->address.ip, ":", peer->address.port,
			" | State: ", static_cast<int>(peer->state), " | Ping: ", peer->pingMs, "ms\n");
		}
	}

	void CmdConnect(std::istringstream& iss)
	{
		std::string addrStr;
		if (!(iss >> addrStr))
		{
			Logger::Print(Logger::YELLOW, "usage: connect <ip>:<port>\n");
			return;
		}

		const size_t colon = addrStr.find(':');
		if (colon == std::string::npos)
		{
			Logger::Print(Logger::YELLOW, "Invalid address format. Use ip:port\n");
			return;
		}

		const std::string ip = addrStr.substr(0, colon);
		const u16 port = static_cast<u16>(std::stoi(addrStr.substr(colon + 1)));

		const auto id = Transport::Connect(ip, port);

		if (!id)
		{
			Logger::Print(Logger::RED, "Failed to connect to ", ip, ":", port, "\n");
			return;
		}

		Logger::Print(Logger::GREEN, "Connecting to ", ip, ":", port, " (peer ", *id, ")\n");
	}

	void CmdDisconnect(std::istringstream& iss)
	{
		Networking::PeerId id = 0;
		if (!(iss >> id))
		{
			Logger::Print(Logger::YELLOW, "usage: disconnect <peerId>\n");
			return;
		}

		Transport::Disconnect(id);
	}

	void CmdClose(std::istringstream& iss)
	{
		Transport::NodeId id = 0;
		if (!(iss >> id))
		{
			Logger::Print(Logger::YELLOW, "usage: close <nodeId>\n");
			return;
		}

		Transport::Close(id);
		Logger::Print(Logger::GREEN, "Close requested for node ", id, "\n");
	}

	void CmdSend(std::istringstream& iss)
	{
		std::string target;
		if (!(iss >> target))
		{
			Logger::Print(Logger::YELLOW, "usage: send <nodeId|name> <text>\n");
			return;
		}

		std::string text;
		std::getline(iss, text);

		if (!text.empty() && text.front() == ' ')
		{
			text.erase(text.begin());
		}

		const auto id = ResolveTarget(target);
		if (!id)
		{
			Logger::Print(Logger::YELLOW, "Unknown target: ", target, "\n");
			return;
		}

		const auto data = ToBytes(text);

		if (!Transport::Send(*id, data, 0, true))
		{
			Logger::Print(Logger::RED, "Send to ", target, " failed\n");
			return;
		}

		Logger::Print(Logger::GREEN, "Queued ", data.size(), " bytes to ", target, " (node ", *id,
		") on channel 0 reliable\n");
	}

	void CmdSendChannel(std::istringstream& iss)
	{
		std::string target;
		u32 channel = 0;

		if (!(iss >> target >> channel))
		{
			Logger::Print(Logger::YELLOW, "usage: sendc <nodeId|name> <channel> <text>\n");
			return;
		}

		std::string text;
		std::getline(iss, text);

		if (!text.empty() && text.front() == ' ')
		{
			text.erase(text.begin());
		}

		const auto id = ResolveTarget(target);
		if (!id)
		{
			Logger::Print(Logger::YELLOW, "Unknown target: ", target, "\n");
			return;
		}

		const auto data = ToBytes(text);

		if (!Transport::Send(*id, data, static_cast<Transport::ChannelId>(channel), true))
		{
			Logger::Print(Logger::RED, "Send to ", target, " failed\n");
			return;
		}

		Logger::Print(Logger::GREEN, "Queued ", data.size(), " bytes to ", target, " on channel ", channel,
		" reliable\n");
	}

	void CmdRaw(std::istringstream& iss)
	{
		std::string target;
		if (!(iss >> target))
		{
			Logger::Print(Logger::YELLOW, "usage: raw <nodeId|name> <text>\n");
			return;
		}

		std::string text;
		std::getline(iss, text);

		if (!text.empty() && text.front() == ' ')
		{
			text.erase(text.begin());
		}

		const auto id = ResolveTarget(target);
		if (!id)
		{
			Logger::Print(Logger::YELLOW, "Unknown target: ", target, "\n");
			return;
		}

		const auto data = ToBytes(text);

		if (!Transport::Send(*id, data, 0, false))
		{
			Logger::Print(Logger::RED, "Raw send to ", target, " failed\n");
			return;
		}

		Logger::Print(Logger::GREEN, "Queued ", data.size(), " bytes raw to ", target, "\n");
	}

	void CmdBig(std::istringstream& iss)
	{
		std::string target;
		u32 size = 0;

		if (!(iss >> target >> size))
		{
			Logger::Print(Logger::YELLOW, "usage: big <nodeId|name> <bytes>\n");
			return;
		}

		const auto id = ResolveTarget(target);
		if (!id)
		{
			Logger::Print(Logger::YELLOW, "Unknown target: ", target, "\n");
			return;
		}

		std::vector<std::byte> data(size);
		for (u32 i = 0; i < size; ++i)
		{
			data[i] = static_cast<std::byte>('A' + (i % 26));
		}

		if (!Transport::Send(*id, data, 0, true))
		{
			Logger::Print(Logger::RED, "Big send to ", target, " failed\n");
			return;
		}

		Logger::Print(Logger::GREEN, "Queued ", size, " byte buffer to ", target, "\n");
	}

	void CmdStress(std::istringstream& iss)
	{
		std::string target;
		u32 count = 0;
		u32 size = 0;

		if (!(iss >> target >> count >> size))
		{
			Logger::Print(Logger::YELLOW, "usage: stress <nodeId|name> <count> <size>\n");
			return;
		}

		const auto id = ResolveTarget(target);
		if (!id)
		{
			Logger::Print(Logger::YELLOW, "Unknown target: ", target, "\n");
			return;
		}

		std::vector<std::byte> data(size);
		for (u32 i = 0; i < size; ++i)
		{
			data[i] = static_cast<std::byte>('0' + (i % 10));
		}

		u32 sent = 0;

		for (u32 i = 0; i < count; ++i)
		{
			if (!Transport::Send(*id, data, 0, true))
			{
				break;
			}

			sent++;
		}

		Logger::Print(Logger::GREEN, "Queued ", sent, " of ", count, " messages (", size, " bytes each)\n");
	}

	void ProcessCommand(const std::string& cmd)
	{
		std::istringstream iss(cmd);
		std::string action;
		iss >> action;

		if (action.empty())
		{
			return;
		}

		if (action == "quit" || action == "q")
		{
			s_running = false;
			return;
		}

		if (action == "help" || action == "h")
		{
			PrintHelp();
			return;
		}

		if (action == "sessions")
		{
			CmdSessions();
			return;
		}

		if (action == "nodes")
		{
			CmdNodes();
			return;
		}

		if (action == "peers")
		{
			CmdPeers();
			return;
		}

		if (action == "connect")
		{
			CmdConnect(iss);
			return;
		}

		if (action == "disconnect")
		{
			CmdDisconnect(iss);
			return;
		}

		if (action == "close")
		{
			CmdClose(iss);
			return;
		}

		if (action == "send")
		{
			CmdSend(iss);
			return;
		}

		if (action == "sendc")
		{
			CmdSendChannel(iss);
			return;
		}

		if (action == "raw")
		{
			CmdRaw(iss);
			return;
		}

		if (action == "big")
		{
			CmdBig(iss);
			return;
		}

		if (action == "stress")
		{
			CmdStress(iss);
			return;
		}

		Logger::Print(Logger::YELLOW, "Unknown command: ", cmd, "\n");
	}
}

int main() // NOLINT
{
	Logger::SetLogLevel(LogLevel::INFO);

	Logger::Print("Enter starting port (0 for client mode): ");
	u16 port = 0;
	std::cin >> port;
	std::cin.ignore();

	Logger::Print("Enter node name: ");
	std::string name;
	std::getline(std::cin, name);

	if (!Transport::Init(port, name))
	{
		Logger::Print(Logger::RED, "Failed to init transport on port: ", port, "\n");
		return 1;
	}

	Logger::Print(Logger::GREEN, "Transport ready.\n");
	Logger::Print("  Self id: ", Transport::GetSelfId(), "\n");
	Logger::Print("  Name:    ", name, "\n");
	Logger::Print("  Port:    ", Networking::GetLocalPort(), port == 0 ? " (client, ephemeral)" : " (server)", "\n\n");

	PrintHelp();

	std::thread input(InputThread);

	std::queue<Transport::Event> events;

	while (s_running)
	{
		{
			std::lock_guard<std::mutex> lock(s_cmdMutex);
			while (!s_commands.empty())
			{
				std::string cmd = s_commands.front();
				s_commands.pop();

				ProcessCommand(cmd);

				if (!s_running)
				{
					break;
				}
			}
		}

		if (!s_running)
		{
			break;
		}

		Transport::Poll(events, 100);

		while (!events.empty())
		{
			auto& e = events.front();

			if (e.type == Transport::EventType::RECEIVE)
			{
				Logger::Print(Logger::CYAN, "[msg] node=", e.nodeId, " chan=", static_cast<int>(e.channelId),
				" size=", e.data.size(), " : ", FromBytes(e.data), "\n");
			}

			events.pop();
		}
	}

	Logger::Print(Logger::GRAY, "Test platform shutting down\n");

	if (input.joinable())
	{
		input.detach();
	}

	Transport::Shutdown();

	return 0;
}