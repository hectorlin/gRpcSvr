#include "LoggingInterceptor.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace hello {

LoggingInterceptor::LoggingInterceptor(grpc::experimental::ServerRpcInfo* info) 
    : info_(info), startTime_(std::chrono::steady_clock::now()) {
}

void LoggingInterceptor::Intercept(grpc::experimental::InterceptorBatchMethods* methods) {
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
        logRequest(info_->method());
    }
    
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::POST_SEND_MESSAGE)) {
        // Log after sending message
    }
    
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
        grpc::Status status = methods->GetSendStatus();
        logResponse(info_->method(), status);
    }
    
    methods->Proceed();
}

void LoggingInterceptor::logRequest(const std::string& methodName) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    std::cout << "[" << oss.str() << "] REQUEST: " << methodName << std::endl;
}

void LoggingInterceptor::logResponse(const std::string& methodName, grpc::Status status) {
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime_);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    std::string statusStr = status.ok() ? "OK" : "ERROR";
    std::cout << "[" << oss.str() << "] RESPONSE: " << methodName 
              << " - Status: " << statusStr 
              << " - Duration: " << duration.count() << "ms" << std::endl;
}

grpc::experimental::Interceptor* LoggingInterceptorFactory::CreateServerInterceptor(
    grpc::experimental::ServerRpcInfo* info) {
    return new LoggingInterceptor(info);
}

} // namespace hello 