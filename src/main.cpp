#include "NetworkCore.h"
#include "NetworkTypes.h"

#include "Log/Log.h"

int main(int argc, char* argv[])
{
	NetworkCore core;

	if (argc == 2 && std::string(argv[1]) == "-c")
	{
		Output("Starting client");

		core.InitClient(1);

		Log(core.Connect(Address{"127.0.0.1", 3131}));
	}

	else if (argc == 2 &&  std::string(argv[1]) == "-s")
	{
		Output("Starting server");

		core.InitServer(3131, 10, 1);
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
				Log("Someone connected");
			}

			else if (event.type == NetworkEventType::Disconnect)
			{
				Log("Someone disconnected");
			}

			else
			{
				Log("Got something");
			}

			events.pop();
		}
	}

	return 0;
}