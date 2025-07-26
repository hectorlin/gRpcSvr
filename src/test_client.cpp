#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>

// Include generated protobuf files
#include "HelloService.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using hello::HelloService;
using hello::HelloRequest;
using hello::HelloResponse;

class HelloServiceClient {
public:
    HelloServiceClient(std::shared_ptr<Channel> channel)
        : stub_(HelloService::NewStub(channel)) {}

    void SayHello(const std::string& name, int32_t age) {
        HelloRequest request;
        request.set_name(name);
        request.set_age(age);

        HelloResponse response;
        ClientContext context;

        Status status = stub_->SayHello(&context, request, &response);

        if (status.ok()) {
            std::cout << "SayHello Response: " << response.message() 
                      << " (Timestamp: " << response.timestamp() << ")" << std::endl;
        } else {
            std::cout << "SayHello RPC failed: " << status.error_message() << std::endl;
        }
    }

    void SayHelloStream(const std::string& name, int32_t age) {
        HelloRequest request;
        request.set_name(name);
        request.set_age(age);

        ClientContext context;
        std::unique_ptr<grpc::ClientReader<HelloResponse>> reader(
            stub_->SayHelloStream(&context, request));

        HelloResponse response;
        int messageCount = 0;
        while (reader->Read(&response)) {
            messageCount++;
            std::cout << "Stream Message " << messageCount << ": " << response.message() 
                      << " (Timestamp: " << response.timestamp() << ")" << std::endl;
        }

        Status status = reader->Finish();
        if (status.ok()) {
            std::cout << "Stream completed successfully. Received " << messageCount << " messages." << std::endl;
        } else {
            std::cout << "Stream RPC failed: " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<HelloService::Stub> stub_;
};

int main() {
    std::string serverAddress = "localhost:50051";
    
    std::cout << "gRPC Test Client" << std::endl;
    std::cout << "Connecting to server at: " << serverAddress << std::endl;
    
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    HelloServiceClient client(channel);

    // Test unary call
    std::cout << "\n=== Testing SayHello (Unary RPC) ===" << std::endl;
    client.SayHello("Alice", 25);
    client.SayHello("Bob", 30);

    // Test streaming call
    std::cout << "\n=== Testing SayHelloStream (Server Streaming RPC) ===" << std::endl;
    client.SayHelloStream("Charlie", 35);

    std::cout << "\nTest completed." << std::endl;
    return 0;
} 