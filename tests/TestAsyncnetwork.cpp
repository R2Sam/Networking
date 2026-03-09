// test_asyncnetwork.cpp
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include "catch2/matchers/catch_matchers_vector.hpp"
#include <chrono>
#include <iomanip>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include "Networking/AsyncNetwork.h"
#include "Networking/NetworkTypes.h"

using namespace std::chrono_literals;

// Helper function to wait for events with timeout
bool WaitForEvent(AsyncNetwork& network, NetworkEvent& event, std::chrono::milliseconds timeout = 100ms)
{
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout)
    {
        if (network.PopEvent(event))
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

// Helper to clear all pending events
void ClearEvents(AsyncNetwork& network)
{
    NetworkEvent event;
    while (network.PopEvent(event)) {}
}

// Test initialization and shutdown
TEST_CASE("AsyncNetwork initializes and shuts down correctly", "[init]")
{
    AsyncNetwork network;
    
    SECTION("Client initialization")
    {
        REQUIRE(network.InitClient());
        network.ShutDown();
    }
    
    SECTION("Server initialization")
    {
        REQUIRE(network.InitServer(12345, 10, 2));
        network.ShutDown();
    }
    
    SECTION("Double initialization fails")
    {
        REQUIRE(network.InitClient());
        REQUIRE_FALSE(network.InitClient());
        network.ShutDown();
    }
}

// Test client-server connection
TEST_CASE("Client and server can establish connection", "[connection]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    // Initialize
    REQUIRE(server.InitServer(12346, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Connect client to server
    client.Connect(Address{"127.0.0.1", 12346}, 42);
    
    // Wait for connection events
    NetworkEvent serverEvent, clientEvent;
    
    SECTION("Server receives connect event")
    {
        REQUIRE(WaitForEvent(server, serverEvent));
        REQUIRE(serverEvent.type == NetworkEventType::CONNECT);
        REQUIRE(serverEvent.peer.id > 0);
        REQUIRE(serverEvent.peer.address.ip == "::ffff:127.0.0.1");
    }
    
    SECTION("Client receives connect event")
    {
        REQUIRE(WaitForEvent(client, clientEvent));
        REQUIRE(clientEvent.type == NetworkEventType::CONNECT);
    }
    
    // Cleanup
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test data transmission
TEST_CASE("Data can be sent and received between client and server", "[data]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    // Initialize
    REQUIRE(server.InitServer(12347, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Establish connection
    client.Connect(Address{"127.0.0.1", 12347}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    ClearEvents(server);
    ClearEvents(client);
    
    SECTION("Client to server data transfer")
    {
        std::vector<u8> testData = {1, 2, 3, 4, 5};
        std::vector<u8> data = testData;
        client.Send(serverEvent.peer.id, std::move(data), 0, true);
        
        REQUIRE(WaitForEvent(server, serverEvent));
        REQUIRE(serverEvent.type == NetworkEventType::RECEIVE);
        REQUIRE_THAT(serverEvent.data, Catch::Matchers::Equals(testData));
    }
    
    SECTION("Server to client data transfer")
    {
        std::vector<u8> responseData = {6, 7, 8, 9, 10};
        std::vector<u8> data = responseData;
        server.Send(serverEvent.peer.id, std::move(data), 0, true);
        
        REQUIRE(WaitForEvent(client, clientEvent));
        REQUIRE(clientEvent.type == NetworkEventType::RECEIVE);
        REQUIRE_THAT(clientEvent.data, Catch::Matchers::Equals(responseData));
    }
    
    // Cleanup
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test unreliable messages
TEST_CASE("Unreliable messages can be sent", "[data][unreliable]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12348, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Connect
    client.Connect(Address{"127.0.0.1", 12348}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    ClearEvents(server);
    
    // Send unreliable message
    std::vector<u8> testData = {1, 2, 3, 4, 5};
    std::vector<u8> data = testData;
    client.Send(serverEvent.peer.id, std::move(data), 0, false); // unreliable
    
    // Should still receive (in local network it likely arrives)
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(serverEvent.type == NetworkEventType::RECEIVE);
    REQUIRE_THAT(serverEvent.data, Catch::Matchers::Equals(testData));
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test multiple channels
TEST_CASE("Multiple channels work correctly", "[channels]")
{
    const ChannelId CHANNEL1 = 0;
    const ChannelId CHANNEL2 = 1;
    
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12349, 2, 2)); // 2 channels
    REQUIRE(client.InitClient(64, 2)); // 2 channels
    
    server.Start();
    client.Start();
    
    // Connect
    client.Connect(Address{"127.0.0.1", 12349}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    ClearEvents(server);
    
    // Send on channel 1
    std::vector<u8> data1 = {1, 2, 3};
    std::vector<u8> data = data1;
    client.Send(serverEvent.peer.id, std::move(data), CHANNEL1, true);
    
    // Send on channel 2
    std::vector<u8> data2 = {4, 5, 6};
    data = data2;
    client.Send(serverEvent.peer.id, std::move(data), CHANNEL2, true);
    
    // Check received data and channels
    bool receivedChannel1 = false;
    bool receivedChannel2 = false;
    
    for (int i = 0; i < 2; i++)
    {
        REQUIRE(WaitForEvent(server, serverEvent));
        REQUIRE(serverEvent.type == NetworkEventType::RECEIVE);
        
        if (serverEvent.channel == CHANNEL1)
        {
            REQUIRE_THAT(serverEvent.data, Catch::Matchers::Equals(data1));
            receivedChannel1 = true;
        }
        else if (serverEvent.channel == CHANNEL2)
        {
            REQUIRE_THAT(serverEvent.data, Catch::Matchers::Equals(data2));
            receivedChannel2 = true;
        }
    }
    
    REQUIRE(receivedChannel1);
    REQUIRE(receivedChannel2);
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test disconnection
TEST_CASE("Disconnection works correctly", "[disconnect]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12350, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Connect
    client.Connect(Address{"127.0.0.1", 12350}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    PeerId serverPeerId = serverEvent.peer.id;
    
    // Disconnect
    server.Disconnect(serverPeerId, 123);
    
    SECTION("Server receives disconnect event")
    {
        REQUIRE(WaitForEvent(server, serverEvent));
        REQUIRE(serverEvent.type == NetworkEventType::DISCONNECT);
    }
    
    SECTION("Client receives disconnect event")
    {
        REQUIRE(WaitForEvent(client, clientEvent));
        REQUIRE(clientEvent.type == NetworkEventType::DISCONNECT);
    }
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test multiple clients
TEST_CASE("Server can handle multiple clients", "[multi-client]")
{
    AsyncNetwork server;
    AsyncNetwork client1;
    AsyncNetwork client2;
    
    REQUIRE(server.InitServer(12351, 3, 1)); // Max 3 peers
    REQUIRE(client1.InitClient(64, 1));
    REQUIRE(client2.InitClient(64, 1));
    
    server.Start();
    client1.Start();
    client2.Start();
    
    // Connect both clients
    client1.Connect(Address{"127.0.0.1", 12351}, 1);
    client2.Connect(Address{"127.0.0.1", 12351}, 2);
    
    NetworkEvent serverEvent, clientEvent;
    
    // Server receives two connections
    int connections = 0;
    PeerId peerId1 = 0, peerId2 = 0;
    
    for (int i = 0; i < 2; i++)
    {
        REQUIRE(WaitForEvent(server, serverEvent));
        REQUIRE(serverEvent.type == NetworkEventType::CONNECT);
        
        if (i == 0)
            peerId1 = serverEvent.peer.id;
        else
            peerId2 = serverEvent.peer.id;
        connections++;
    }
    REQUIRE(connections == 2);
    
    // Both clients connect
    REQUIRE(WaitForEvent(client1, clientEvent));
    REQUIRE(clientEvent.type == NetworkEventType::CONNECT);
    
    REQUIRE(WaitForEvent(client2, clientEvent));
    REQUIRE(clientEvent.type == NetworkEventType::CONNECT);
    
    // Server sends to both clients
    std::vector<u8> broadcastData = {99, 99, 99};
    std::vector<u8> data = broadcastData;
    server.Send(peerId1, std::move(data), 0, true);
    data = broadcastData;
    server.Send(peerId2, std::move(data), 0, true);
    
    // Both clients receive
    bool client1Received = false;
    bool client2Received = false;
    
    for (int i = 0; i < 2; i++)
    {
        if (WaitForEvent(client1, clientEvent) && clientEvent.type == NetworkEventType::RECEIVE)
        {
            REQUIRE_THAT(clientEvent.data, Catch::Matchers::Equals(broadcastData));
            client1Received = true;
        }
        if (WaitForEvent(client2, clientEvent) && clientEvent.type == NetworkEventType::RECEIVE)
        {
            REQUIRE_THAT(clientEvent.data, Catch::Matchers::Equals(broadcastData));
            client2Received = true;
        }
    }
    
    REQUIRE(client1Received);
    REQUIRE(client2Received);
    
    client1.Stop();
    client2.Stop();
    server.Stop();
    client1.ShutDown();
    client2.ShutDown();
    server.ShutDown();
}

// Test connection failure
TEST_CASE("Connection failure is reported correctly", "[failure]")
{
    AsyncNetwork client;
    
    REQUIRE(client.InitClient(64, 1));
    client.Start();
    
    // Try to connect to non-existent server
    client.Connect(Address{"127.0.0.1", 12399}, 0);
    
    NetworkEvent clientEvent;
    REQUIRE(WaitForEvent(client, clientEvent, 7500ms));
    REQUIRE(clientEvent.type == NetworkEventType::FAILED_CONNECTION);
    
    client.Stop();
    client.ShutDown();
}

// Test sending data before connection
TEST_CASE("Sending with invalid peer ID doesn't crash", "[error-handling]")
{
    AsyncNetwork client;
    
    REQUIRE(client.InitClient(64, 1));
    client.Start();
    
    // Try to send with invalid peer ID
    std::vector<u8> data = {1, 2, 3};
    REQUIRE_NOTHROW(client.Send(999, std::move(data), 0, true));
    
    // Should not produce events
    NetworkEvent clientEvent;
    REQUIRE_FALSE(WaitForEvent(client, clientEvent, 50ms));
    
    client.Stop();
    client.ShutDown();
}

// Test large data transmission
TEST_CASE("Large data can be transmitted", "[performance][large-data]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12352, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Connect
    client.Connect(Address{"127.0.0.1", 12352}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    ClearEvents(server);
    
    // Send large data
    const size_t LARGE_SIZE = 1024 * 1024; // 1MB
    std::vector<u8> largeData(LARGE_SIZE);
    for (size_t i = 0; i < LARGE_SIZE; i++)
        largeData[i] = static_cast<u8>(i % 256);
    
    std::vector<u8> data = largeData;
    client.Send(serverEvent.peer.id, std::move(data), 0, true);
    
    // Server should receive
    REQUIRE(WaitForEvent(server, serverEvent, 1000ms));
    REQUIRE(serverEvent.type == NetworkEventType::RECEIVE);
    REQUIRE(serverEvent.data.size() == LARGE_SIZE);
    REQUIRE_THAT(serverEvent.data, Catch::Matchers::Equals(largeData));
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test concurrent operations
TEST_CASE("Concurrent operations work correctly", "[concurrency]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12353, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Connect
    client.Connect(Address{"127.0.0.1", 12353}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    // Rapidly send many messages
    std::vector<std::thread> senders;
    const int NUM_MESSAGES = 100;
    std::atomic<int> receivedCount(0);
    
    // Start receiver thread
    std::thread receiver([&]()
    {
        NetworkEvent event;
        while (receivedCount < NUM_MESSAGES)
        {
            if (server.PopEvent(event) && event.type == NetworkEventType::RECEIVE)
                receivedCount++;
        }
    });
    
    // Send messages
    for (int i = 0; i < NUM_MESSAGES; i++)
    {
        senders.emplace_back([&, i]()
        {
            std::vector<u8> data = {static_cast<u8>(i)};
            client.Send(serverEvent.peer.id, std::move(data), 0, true);
        });
    }
    
    // Join senders
    for (auto& t : senders)
        t.join();
    
    // Wait for all messages to be received
    receiver.join();
    
    REQUIRE(receivedCount == NUM_MESSAGES);
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test raw pointer send
TEST_CASE("Raw pointer send works correctly", "[data][raw-pointer]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12354, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Connect
    client.Connect(Address{"127.0.0.1", 12354}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    ClearEvents(server);
    
    // Send using raw pointer
    u8 rawData[] = {10, 20, 30, 40, 50};
    client.Send(serverEvent.peer.id, rawData, sizeof(rawData), 0, true);
    
    // Server should receive
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(serverEvent.type == NetworkEventType::RECEIVE);
    REQUIRE(serverEvent.data.size() == sizeof(rawData));
    
    for (size_t i = 0; i < sizeof(rawData); i++)
        REQUIRE(serverEvent.data[i] == rawData[i]);
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test stop/start cycle
TEST_CASE("Stop and start cycle works correctly", "[lifecycle]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12355, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    server.Start();
    client.Start();
    
    // Connect
    client.Connect(Address{"127.0.0.1", 12355}, 0);
    
    NetworkEvent serverEvent, clientEvent;
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    // Stop both
    client.Stop();
    server.Stop();
    
    // Should not receive events when stopped
    ClearEvents(server);
    ClearEvents(client);
    
    // Try to send while stopped (should not crash)
    std::vector<u8> data = {1, 2, 3};
    REQUIRE_NOTHROW(client.Send(serverEvent.peer.id, std::move(data), 0, true));
    
    REQUIRE_FALSE(WaitForEvent(server, serverEvent, 50ms));
    REQUIRE_FALSE(WaitForEvent(client, clientEvent, 50ms));
    
    // Restart
    server.Start();
    client.Start();
    
    // Should be able to connect again
    client.Connect(Address{"127.0.0.1", 12355}, 0);
    
    REQUIRE(WaitForEvent(server, serverEvent));
    REQUIRE(WaitForEvent(client, clientEvent));
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test commands before start
TEST_CASE("Commands queued before start are processed", "[queuing]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12356, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    // Queue commands before starting
    client.Connect(Address{"127.0.0.1", 12356}, 0);
    
    std::vector<u8> data = {1, 2, 3};
    client.Send(1, std::move(data), 0, true);
    
    // Now start - should process queued commands
    server.Start();
    client.Start();
    
    NetworkEvent serverEvent, clientEvent;
    
    // Should eventually connect
    REQUIRE(WaitForEvent(server, serverEvent, 500ms));
    REQUIRE(serverEvent.type == NetworkEventType::CONNECT);
    
    client.Stop();
    server.Stop();
    client.ShutDown();
    server.ShutDown();
}

// Test multiple connections/disconnections
TEST_CASE("Multiple connection/disconnection cycles work", "[cycles]")
{
    AsyncNetwork server;
    AsyncNetwork client;
    
    REQUIRE(server.InitServer(12357, 2, 1));
    REQUIRE(client.InitClient(64, 1));
    
    for (int cycle = 0; cycle < 3; cycle++)
    {
        server.Start();
        client.Start();
        
        // Connect
        client.Connect(Address{"127.0.0.1", 12357}, cycle);
        
        NetworkEvent serverEvent, clientEvent;
        REQUIRE(WaitForEvent(server, serverEvent));
        REQUIRE(WaitForEvent(client, clientEvent));
        
        // Disconnect
        client.Disconnect(clientEvent.peer.id, cycle);
        
        REQUIRE(WaitForEvent(server, serverEvent, 200ms));
        REQUIRE(WaitForEvent(client, clientEvent, 200ms));
        
        client.Stop();
        server.Stop();
        
        // Small delay to ensure clean state
        std::this_thread::sleep_for(50ms);
    }
    
    client.ShutDown();
    server.ShutDown();
}