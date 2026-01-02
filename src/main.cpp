#include "NetworkCore.h"
#include "NetworkTypes.h"

#include "Log/Log.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h> 

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/websocket.h>
#include <emscripten/threading.h>
#include <emscripten/posix_socket.h>

static EMSCRIPTEN_WEBSOCKET_T bridgeSocket = 0;
#endif

int main()
{
// #ifdef __EMSCRIPTEN__
//   bridgeSocket = emscripten_init_websocket_to_posix_socket_bridge("ws://localhost:8080");
//   // Synchronously wait until connection has been established.
//   uint16_t readyState = 0;
//   do {
//     emscripten_websocket_get_ready_state(bridgeSocket, &readyState);
//     emscripten_thread_sleep(100);
//   } while (readyState == 0);
// #endif

	NetworkCore core;

	bool isServer;

	if (true)
	{
		Log("Starting client");

		isServer = false;

		Log(core.InitServer(3131));

		Log("Attempting to connect");
		core.Connect(Address{"127.0.0.1", 1313});
	}

	else if (false)
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
		core.Poll(events, 0);

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
