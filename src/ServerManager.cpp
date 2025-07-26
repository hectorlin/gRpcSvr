#include "ServerManager.h"
#include "HelloService.h"
#include "LoggingInterceptor.h"
#include <iostream>
#include <grpcpp/grpcpp.h>

namespace hello {

ServerManager& ServerManager::getInstance() {
    static ServerManager instance;
    return instance;
}

bool ServerManager::startServer(const std::string& serverAddress) {
    if (running_.load()) {
        std::cout << "Server is already running" << std::endl;
        return false;
    }
    
    serverAddress_ = serverAddress;
    
    // Create service
    service_ = std::make_unique<HelloServiceImpl>();
    
    // Create interceptor factory
    interceptorFactory_ = std::make_unique<LoggingInterceptorFactory>();
    
    // Build server with optimized settings
    grpc::ServerBuilder builder;
    
    // Optimized: Configure thread pool size for better concurrency
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS, 4);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, 4);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, 16);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::CQ_TIMEOUT_MSEC, 10000);
    
    // Optimized: Set maximum message size and other limits
    builder.SetMaxReceiveMessageSize(INT_MAX);
    builder.SetMaxSendMessageSize(INT_MAX);
    
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    
    std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>> interceptors;
    interceptors.push_back(std::make_unique<LoggingInterceptorFactory>());
    builder.experimental().SetInterceptorCreators(std::move(interceptors));
    
    server_ = builder.BuildAndStart();
    if (!server_) {
        std::cerr << "Failed to start server on " << serverAddress << std::endl;
        return false;
    }
    
    running_.store(true);
    std::cout << "gRPC Server started on " << serverAddress << std::endl;
    std::cout << "Optimized for high performance with enhanced thread pool" << std::endl;
    
    // Start server in a separate thread
    serverThread_ = std::thread([this]() {
        server_->Wait();
    });
    
    return true;
}

void ServerManager::stopServer() {
    if (!running_.load()) {
        std::cout << "Server is not running" << std::endl;
        return;
    }
    
    std::cout << "Stopping gRPC server..." << std::endl;
    server_->Shutdown();
    
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    running_.store(false);
    std::cout << "gRPC Server stopped" << std::endl;
}

bool ServerManager::isRunning() const {
    return running_.load();
}

} // namespace hello 