#pragma once

#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

// Forward declarations
namespace hello {
    class HelloServiceImpl;
    class LoggingInterceptorFactory;
}

namespace hello {

class ServerManager {
public:
    static ServerManager& getInstance();
    
    // Delete copy constructor and assignment operator
    ServerManager(const ServerManager&) = delete;
    ServerManager& operator=(const ServerManager&) = delete;
    
    bool startServer(const std::string& serverAddress);
    void stopServer();
    bool isRunning() const;
    
private:
    ServerManager() = default;
    ~ServerManager() = default;
    
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<HelloServiceImpl> service_;
    std::unique_ptr<LoggingInterceptorFactory> interceptorFactory_;
    std::string serverAddress_;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
};

} // namespace hello 