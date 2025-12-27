#include "NetworkCore.h"
#include "NetworkTypes.h"

#include "Log/Log.h"

#include <cstring>

int main(int argc, char* argv[])
{
	NetworkCore core;

	bool isServer;

	if (argc == 2 && std::string(argv[1]) == "-c")
	{
		Log("Starting client");

		isServer = false;

		core.InitClient();

		core.Connect(Address{"127.0.0.1", 1313});
	}

	else if (argc == 2 &&  std::string(argv[1]) == "-s")
	{
		Log("Starting server");

		isServer = true;

		core.InitServer(1313);
	}

	else
	{
		Output("No args provided, -c for client or -s for server");
		return 0;
	}

	std::queue<NetworkEvent> events;

	while(true)
	{
		core.Poll(events, 100);

		while(events.size())
		{
			NetworkEvent& event = events.front();

			if (event.type == NetworkEventType::Connect)
			{
				Log("Someone connected from: ", event.address.ip, ":", event.address.port);

				if (isServer)
				{
					const char* message = "Hello there from server";
					core.Send(event.peer , std::vector<u8>(message, message + strlen(message)));
				}

				else
				{
					const char* message = "Hello there from client";
					core.Send(event.peer, std::vector<u8>(message, message + strlen(message)));
				}
			}

			else if (event.type == NetworkEventType::Disconnect)
			{
				Log("Someone disconnected");
			}

			else if (event.type == NetworkEventType::Receive)
			{
				std::string message((char*)event.data.data(), event.data.size());
				Log("Got something");
				Output("Size: ", event.data.size());
				Output("Data: ", message);
			}

			else
			{
				Log("Failed to connect to: ", event.address.ip, ":", event.address.port);
			}

			events.pop();
		}
	}

	return 0;
}