#include "Networking/Node.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <thread>

static std::mutex s_cmdMutex;
static std::queue<std::string> s_commands;
static std::atomic<bool> s_running{true};

static bool IsNumeric(const std::string& s)
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

static void InputThread()
{
	std::string line;

	while (s_running && std::getline(std::cin, line))
	{
		std::lock_guard<std::mutex> lock(s_cmdMutex);
		s_commands.push(line);
	}

	s_running = false;
}

int main() // NOLINT
{
	std::cout << "Enter starting port: ";
	u16 port;
	std::cin >> port;
	std::cin.ignore(); // consume newline

	std::cout << "Enter node name: ";
	std::string name;
	std::getline(std::cin, name);

	if (!Node::Init(port, name))
	{
		std::cerr << "Failed to init server on port " << port << "\n";
		return 1;
	}

	std::cout << "Server started on port " << port << " as \"" << name << "\".\n"
			  << "Commands:\n"
			  << "  connect <ip>:<port>         - connect to a peer\n"
			  << "  disconnect <peerId>         - disconnect a peer\n"
			  << "  send <nodeId|name> <text>   - send a message\n"
			  << "  list                        - lists all direct peers\n"
			  << "  nodes                       - lists all known nodes\n"
			  << "  quit                        - exit\n\n";

	std::thread input(InputThread);

	std::queue<Networking::Event> events;

	while (s_running)
	{
		// Process user commands
		{
			std::lock_guard<std::mutex> lock(s_cmdMutex);
			while (!s_commands.empty())
			{
				std::string cmd = s_commands.front();
				s_commands.pop();

				if (cmd == "quit" || cmd == "q")
				{
					s_running = false;
					break;
				}

				if (cmd == "list")
				{
					std::cout << "Peers:\n";

					std::vector<Networking::PeerId> ids = Networking::GetPeerIds();
					for (const auto id : ids)
					{
						Networking::Peer peer = *Networking::GetPeer(id);

						std::cout << " | PeerId: " << peer.id << " | Address: " << peer.address.ip << ":"
								  << peer.address.port << " | State: " << static_cast<int>(peer.state) << "\n";
					}

					std::cout << "\n";
					continue;
				}

				if (cmd == "nodes")
				{
					std::cout << "Nodes:\n";

					std::vector<std::pair<Node::Node, Networking::PeerId>> routes = Node::GetRoutes();

					for (const auto& [node, nextHop] : routes)
					{
						std::cout << " | NodeId: " << node.id << " | Name: " << (node.name.empty() ? "-" : node.name)
								  << " | NextHop(conn): " << nextHop << "\n";
					}

					std::cout << "\n";
					continue;
				}

				std::istringstream iss(cmd);
				std::string action;
				iss >> action;

				if (action == "connect")
				{
					std::string addrStr;
					iss >> addrStr;
					size_t colon = addrStr.find(':');

					if (colon == std::string::npos)
					{
						std::cout << "Invalid address format. Use ip:port\n";
						continue;
					}

					std::string ip = addrStr.substr(0, colon);
					u16 port = static_cast<u16>(std::stoi(addrStr.substr(colon + 1)));
					Networking::Address addr{ip, port};
					std::optional<Networking::PeerId> id = Node::Connect(addr);

					if (id == std::nullopt)
					{
						std::cout << "Failed to connect to " << ip << ":" << port << "\n";
					}

					else
					{
						std::cout << "Connecting to " << ip << ":" << port << " with peer id " << id.value() << "\n";
					}
				}

				else if (action == "disconnect")
				{
					Networking::PeerId id;
					iss >> id;
					Node::Disconnect(id);
				}

				else if (action == "send")
				{
					std::string target;
					if (!(iss >> target))
					{
						std::cout << "usage: send <nodeId|name> <text>\n";
						continue;
					}

					std::string text;
					std::getline(iss, text);
					// trim leading space
					if (!text.empty() && text.front() == ' ')
					{
						text.erase(text.begin());
					}

					std::vector<std::byte> data(text.size());
					memcpy(data.data(), text.data(), text.size());

					bool ok = false;

					if (IsNumeric(target))
					{
						Node::NodeId id = std::stoull(target);
						ok = Node::Send(id, data);
					}

					else
					{
						ok = Node::Send(target, data);
					}

					if (ok)
					{
						std::cout << "Sent " << data.size() << " bytes to " << target << "\n";
					}

					else
					{
						std::cout << "Failed to send to " << target
								  << " (unknown name, or no route and pending queue full)\n";
					}
				}

				else
				{
					std::cout << "Unknown command: " << cmd << "\n";
				}
			}
		}

		// Poll networking for events
		Node::Poll(events, 100);

		while (!events.empty())
		{
			Networking::Event& e = events.front();
			const char* eventTypeStr;
			switch (e.type)
			{
			case Networking::EventType::CONNECT:
				eventTypeStr = "CONNECT";
				break;
			case Networking::EventType::FAILED_CONNECTION:
				eventTypeStr = "FAILED_CONNECTION";
				break;
			case Networking::EventType::DISCONNECT:
				eventTypeStr = "DISCONNECT";
				break;
			case Networking::EventType::RECEIVE:
				eventTypeStr = "RECEIVE";
				break;
			}

			std::cout << "Event: " << eventTypeStr << " | PeerId: " << e.peer.id << " | Address: " << e.peer.address.ip
					  << ":" << e.peer.address.port << " | State: " << static_cast<int>(e.peer.state)
					  << " | Channel: " << static_cast<int>(e.channelId) << "\n";

			if (e.type == Networking::EventType::RECEIVE)
			{
				std::cout << "       Message: ";

				for (const auto c : e.data)
				{
					std::cout << static_cast<char>(c);
				}

				std::cout << "\n";
			}

			events.pop();
		}
	}

	if (input.joinable())
	{
		input.detach();
	}

	Node::Shutdown();

	return 0;
}