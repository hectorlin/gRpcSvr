#include "ServerManager.h"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

int main() {
    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "Starting gRPC Server with C++17..." << std::endl;
    std::cout << "Features: Service, Interceptor, Singleton Pattern" << std::endl;
    
    // Get singleton instance and start server
    auto& serverManager = hello::ServerManager::getInstance();
    
    const std::string serverAddress = "0.0.0.0:50051";
    
    if (!serverManager.startServer(serverAddress)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;
    
    // Keep main thread alive
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop server
    serverManager.stopServer();
    
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
} 