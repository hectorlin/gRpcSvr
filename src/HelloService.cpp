#include "HelloService.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

namespace hello {

grpc::Status HelloServiceImpl::SayHello(grpc::ServerContext* /*context*/, 
                                       const HelloRequest* request, 
                                       HelloResponse* response) {
    // Optimized: Remove console output for better performance
    // std::cout << "SayHello called with name: " << request->name() 
    //           << ", age: " << request->age() << std::endl;
    
    // Optimized: Pre-allocate string capacity
    std::string message;
    message.reserve(100); // Pre-allocate space for typical response
    
    // Optimized: Use string concatenation instead of ostringstream for better performance
    message = "Hello, ";
    message += request->name();
    message += "! You are ";
    message += std::to_string(request->age());
    message += " years old. Welcome to gRPC!";
    
    response->set_message(std::move(message));
    
    // Optimized: Use high_resolution_clock for more precise timing
    response->set_timestamp(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count());
    
    return grpc::Status::OK;
}

grpc::Status HelloServiceImpl::SayHelloStream(grpc::ServerContext* /*context*/, 
                                             const HelloRequest* request, 
                                             grpc::ServerWriter<HelloResponse>* writer) {
    // Optimized: Remove console output for better performance
    // std::cout << "SayHelloStream called with name: " << request->name() 
    //           << ", age: " << request->age() << std::endl;
    
    // Optimized: Pre-allocate response object and reuse it
    HelloResponse response;
    std::string baseMessage;
    baseMessage.reserve(100);
    baseMessage = "Hello, ";
    baseMessage += request->name();
    baseMessage += "! You are ";
    baseMessage += std::to_string(request->age());
    baseMessage += " years old. Welcome to gRPC!";
    
    for (int i = 0; i < 5; ++i) {
        // Optimized: Reuse the same response object
        std::string message = baseMessage + " (stream message " + std::to_string(i + 1) + ")";
        response.set_message(std::move(message));
        response.set_timestamp(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count());
        
        if (!writer->Write(response)) {
            std::cerr << "Failed to write stream response" << std::endl;
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to write stream");
        }
        
        // Optimized: Reduce sleep time for faster streaming
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return grpc::Status::OK;
}

std::string HelloServiceImpl::generateResponse(const std::string& name, int32_t age) {
    // Optimized: Use string concatenation instead of ostringstream
    std::string response;
    response.reserve(100);
    response = "Hello, ";
    response += name;
    response += "! You are ";
    response += std::to_string(age);
    response += " years old. Welcome to gRPC!";
    return response;
}

} // namespace hello 