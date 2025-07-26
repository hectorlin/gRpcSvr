#pragma once

#include <grpcpp/grpcpp.h>
#include <iostream>
#include <chrono>
#include <string>

namespace hello {

class LoggingInterceptor : public grpc::experimental::Interceptor {
public:
    LoggingInterceptor(grpc::experimental::ServerRpcInfo* info);
    
    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override;

private:
    grpc::experimental::ServerRpcInfo* info_;
    std::chrono::steady_clock::time_point startTime_;
    
    void logRequest(const std::string& methodName);
    void logResponse(const std::string& methodName, grpc::Status status);
};

class LoggingInterceptorFactory : public grpc::experimental::ServerInterceptorFactoryInterface {
public:
    grpc::experimental::Interceptor* CreateServerInterceptor(grpc::experimental::ServerRpcInfo* info) override;
};

} // namespace hello 