#include "EpollServer.h"
#include <iostream>
#include <signal.h>
#include <atomic>

std::atomic<bool> running(true);

void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ". Shutting down gracefully..." << std::endl;
    running = false;
}

void printStats(const hello::EpollServer::ServerStats& stats) {
    std::cout << "\n=== EpollServer Statistics ===" << std::endl;
    std::cout << "Total Connections: " << stats.total_connections.load() << std::endl;
    std::cout << "Active Connections: " << stats.active_connections.load() << std::endl;
    std::cout << "Total Requests: " << stats.total_requests.load() << std::endl;
    std::cout << "Total Bytes Sent: " << stats.total_bytes_sent.load() << " bytes" << std::endl;
    std::cout << "Total Bytes Received: " << stats.total_bytes_received.load() << " bytes" << std::endl;
    std::cout << "Epoll Events Processed: " << stats.epoll_events_processed.load() << std::endl;
    std::cout << "=================================" << std::endl;
}

int main() {
    std::cout << "🚀 Starting Epoll-Optimized gRPC Server" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "Features:" << std::endl;
    std::cout << "✓ Epoll-based I/O multiplexing" << std::endl;
    std::cout << "✓ Edge-triggered event handling" << std::endl;
    std::cout << "✓ Non-blocking socket operations" << std::endl;
    std::cout << "✓ Multi-threaded worker pool" << std::endl;
    std::cout << "✓ Connection pooling and cleanup" << std::endl;
    std::cout << "✓ High-performance HTTP/2 handling" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Get server instance
    auto& server = hello::EpollServer::getInstance();
    
    // Start server
    const std::string address = "0.0.0.0";
    const uint16_t port = 50052; // Different port to avoid conflicts
    
    if (!server.startServer(address, port)) {
        std::cerr << "Failed to start EpollServer" << std::endl;
        return 1;
    }
    
    std::cout << "EpollServer is running. Press Ctrl+C to stop." << std::endl;
    
    // Main loop with periodic stats
    auto last_stats_time = std::chrono::steady_clock::now();
    
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // Print stats every 30 seconds
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time > std::chrono::seconds(30)) {
            printStats(server.getStats());
            last_stats_time = now;
        }
    }
    
    // Stop server
    server.stopServer();
    
    // Final stats
    std::cout << "\n=== Final Statistics ===" << std::endl;
    printStats(server.getStats());
    
    std::cout << "EpollServer shutdown complete." << std::endl;
    return 0;
} 