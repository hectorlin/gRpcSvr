#pragma once

#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <chrono>

// Forward declarations
namespace hello {
    class HelloService;
}

// Generated protobuf includes
#include "HelloService.grpc.pb.h"

namespace hello {

class HelloServiceImpl final : public HelloService::Service {
public:
    grpc::Status SayHello(grpc::ServerContext* context, 
                         const HelloRequest* request, 
                         HelloResponse* response) override;
    
    grpc::Status SayHelloStream(grpc::ServerContext* context, 
                               const HelloRequest* request, 
                               grpc::ServerWriter<HelloResponse>* writer) override;

private:
    std::string generateResponse(const std::string& name, int32_t age);
};

} // namespace hello 