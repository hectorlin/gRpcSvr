#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
#include <fstream>
#include <numeric>
#include <algorithm>

// Include generated protobuf files
#include "HelloService.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using hello::HelloService;
using hello::HelloRequest;
using hello::HelloResponse;

class PerformanceTestClient {
public:
    PerformanceTestClient(std::shared_ptr<Channel> channel)
        : stub_(HelloService::NewStub(channel)) {}

    // Test unary RPC performance
    struct UnaryTestResult {
        int64_t total_requests;
        int64_t successful_requests;
        int64_t failed_requests;
        double avg_latency_ms;
        double min_latency_ms;
        double max_latency_ms;
        double p50_latency_ms;
        double p95_latency_ms;
        double p99_latency_ms;
        double throughput_rps;
        std::vector<double> latencies;
    };

    UnaryTestResult testUnaryPerformance(const std::string& name, int32_t age, int num_requests, int num_threads) {
        UnaryTestResult result = {};
        result.total_requests = num_requests;
        std::vector<double> all_latencies;
        std::atomic<int> success_count{0};
        std::atomic<int> failure_count{0};
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Create threads for concurrent requests
        std::vector<std::thread> threads;
        int requests_per_thread = num_requests / num_threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < requests_per_thread; ++i) {
                    HelloRequest request;
                    request.set_name(name + "_" + std::to_string(t) + "_" + std::to_string(i));
                    request.set_age(age);

                    HelloResponse response;
                    ClientContext context;

                    auto req_start = std::chrono::high_resolution_clock::now();
                    Status status = stub_->SayHello(&context, request, &response);
                    auto req_end = std::chrono::high_resolution_clock::now();

                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(req_end - req_start).count() / 1000.0;
                    
                    {
                        std::lock_guard<std::mutex> lock(latency_mutex_);
                        all_latencies.push_back(latency);
                    }

                    if (status.ok()) {
                        success_count++;
                    } else {
                        failure_count++;
                    }
                }
            });
        }

        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        // Calculate statistics
        result.successful_requests = success_count.load();
        result.failed_requests = failure_count.load();
        result.latencies = all_latencies;
        
        if (!all_latencies.empty()) {
            std::sort(all_latencies.begin(), all_latencies.end());
            
            result.avg_latency_ms = std::accumulate(all_latencies.begin(), all_latencies.end(), 0.0) / all_latencies.size();
            result.min_latency_ms = all_latencies.front();
            result.max_latency_ms = all_latencies.back();
            result.p50_latency_ms = all_latencies[all_latencies.size() * 0.5];
            result.p95_latency_ms = all_latencies[all_latencies.size() * 0.95];
            result.p99_latency_ms = all_latencies[all_latencies.size() * 0.99];
        }
        
        result.throughput_rps = (result.successful_requests * 1000.0) / total_duration;
        
        return result;
    }

    // Test streaming RPC performance
    struct StreamingTestResult {
        int64_t total_requests;
        int64_t successful_requests;
        int64_t failed_requests;
        double avg_latency_ms;
        double throughput_rps;
        int64_t total_messages_received;
    };

    StreamingTestResult testStreamingPerformance(const std::string& name, int32_t age, int num_requests) {
        StreamingTestResult result = {};
        result.total_requests = num_requests;
        std::vector<double> latencies;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_requests; ++i) {
            HelloRequest request;
            request.set_name(name + "_" + std::to_string(i));
            request.set_age(age);

            ClientContext context;
            std::unique_ptr<grpc::ClientReader<HelloResponse>> reader(
                stub_->SayHelloStream(&context, request));

            auto req_start = std::chrono::high_resolution_clock::now();
            
            HelloResponse response;
            int message_count = 0;
            while (reader->Read(&response)) {
                message_count++;
            }

            auto req_end = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(req_end - req_start).count() / 1000.0;
            latencies.push_back(latency);

            Status status = reader->Finish();
            if (status.ok()) {
                result.successful_requests++;
                result.total_messages_received += message_count;
            } else {
                result.failed_requests++;
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        if (!latencies.empty()) {
            result.avg_latency_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        }
        
        result.throughput_rps = (result.successful_requests * 1000.0) / total_duration;
        
        return result;
    }

private:
    std::unique_ptr<HelloService::Stub> stub_;
    std::mutex latency_mutex_;
};

void printUnaryTestResult(const PerformanceTestClient::UnaryTestResult& result, const std::string& test_name) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "UNARY RPC PERFORMANCE TEST: " << test_name << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total Requests:     " << result.total_requests << std::endl;
    std::cout << "Successful:         " << result.successful_requests << std::endl;
    std::cout << "Failed:             " << result.failed_requests << std::endl;
    std::cout << "Success Rate:       " << std::fixed << std::setprecision(2) 
              << (result.successful_requests * 100.0 / result.total_requests) << "%" << std::endl;
    std::cout << "Throughput:         " << std::fixed << std::setprecision(2) << result.throughput_rps << " RPS" << std::endl;
    std::cout << "\nLatency Statistics (ms):" << std::endl;
    std::cout << "  Average:          " << std::fixed << std::setprecision(2) << result.avg_latency_ms << std::endl;
    std::cout << "  Min:              " << std::fixed << std::setprecision(2) << result.min_latency_ms << std::endl;
    std::cout << "  Max:              " << std::fixed << std::setprecision(2) << result.max_latency_ms << std::endl;
    std::cout << "  50th Percentile:  " << std::fixed << std::setprecision(2) << result.p50_latency_ms << std::endl;
    std::cout << "  95th Percentile:  " << std::fixed << std::setprecision(2) << result.p95_latency_ms << std::endl;
    std::cout << "  99th Percentile:  " << std::fixed << std::setprecision(2) << result.p99_latency_ms << std::endl;
}

void printStreamingTestResult(const PerformanceTestClient::StreamingTestResult& result, const std::string& test_name) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STREAMING RPC PERFORMANCE TEST: " << test_name << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total Requests:     " << result.total_requests << std::endl;
    std::cout << "Successful:         " << result.successful_requests << std::endl;
    std::cout << "Failed:             " << result.failed_requests << std::endl;
    std::cout << "Success Rate:       " << std::fixed << std::setprecision(2) 
              << (result.successful_requests * 100.0 / result.total_requests) << "%" << std::endl;
    std::cout << "Total Messages:     " << result.total_messages_received << std::endl;
    std::cout << "Avg Messages/Req:   " << std::fixed << std::setprecision(2) 
              << (result.total_messages_received / (double)result.successful_requests) << std::endl;
    std::cout << "Throughput:         " << std::fixed << std::setprecision(2) << result.throughput_rps << " RPS" << std::endl;
    std::cout << "Avg Latency:        " << std::fixed << std::setprecision(2) << result.avg_latency_ms << " ms" << std::endl;
}

void saveResultsToFile(const std::vector<PerformanceTestClient::UnaryTestResult>& unary_results,
                      const std::vector<PerformanceTestClient::StreamingTestResult>& streaming_results) {
    std::ofstream file("performance_report.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open performance_report.txt for writing" << std::endl;
        return;
    }

    file << "gRPC SERVER PERFORMANCE TEST REPORT" << std::endl;
    file << "Generated: " << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
    file << std::string(80, '=') << std::endl << std::endl;

    // Unary RPC Results
    file << "UNARY RPC RESULTS:" << std::endl;
    file << std::string(40, '-') << std::endl;
    for (size_t i = 0; i < unary_results.size(); ++i) {
        const auto& result = unary_results[i];
        file << "Test " << (i + 1) << ":" << std::endl;
        file << "  Requests: " << result.total_requests << std::endl;
        file << "  Success Rate: " << std::fixed << std::setprecision(2) 
             << (result.successful_requests * 100.0 / result.total_requests) << "%" << std::endl;
        file << "  Throughput: " << std::fixed << std::setprecision(2) << result.throughput_rps << " RPS" << std::endl;
        file << "  Avg Latency: " << std::fixed << std::setprecision(2) << result.avg_latency_ms << " ms" << std::endl;
        file << "  P95 Latency: " << std::fixed << std::setprecision(2) << result.p95_latency_ms << " ms" << std::endl;
        file << "  P99 Latency: " << std::fixed << std::setprecision(2) << result.p99_latency_ms << " ms" << std::endl;
        file << std::endl;
    }

    // Streaming RPC Results
    file << "STREAMING RPC RESULTS:" << std::endl;
    file << std::string(40, '-') << std::endl;
    for (size_t i = 0; i < streaming_results.size(); ++i) {
        const auto& result = streaming_results[i];
        file << "Test " << (i + 1) << ":" << std::endl;
        file << "  Requests: " << result.total_requests << std::endl;
        file << "  Success Rate: " << std::fixed << std::setprecision(2) 
             << (result.successful_requests * 100.0 / result.total_requests) << "%" << std::endl;
        file << "  Total Messages: " << result.total_messages_received << std::endl;
        file << "  Throughput: " << std::fixed << std::setprecision(2) << result.throughput_rps << " RPS" << std::endl;
        file << "  Avg Latency: " << std::fixed << std::setprecision(2) << result.avg_latency_ms << " ms" << std::endl;
        file << std::endl;
    }

    file.close();
    std::cout << "\nPerformance report saved to: performance_report.txt" << std::endl;
}

int main() {
    std::string serverAddress = "localhost:50051";
    
    std::cout << "gRPC Performance Test Client" << std::endl;
    std::cout << "Connecting to server at: " << serverAddress << std::endl;
    std::cout << "Make sure the server is running before starting tests!" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    PerformanceTestClient client(channel);

    std::vector<PerformanceTestClient::UnaryTestResult> unary_results;
    std::vector<PerformanceTestClient::StreamingTestResult> streaming_results;

    // Test 1: Low load unary RPC
    std::cout << "\nStarting Test 1: Low Load Unary RPC (100 requests, 1 thread)" << std::endl;
    auto result1 = client.testUnaryPerformance("TestUser", 25, 100, 1);
    printUnaryTestResult(result1, "Low Load (100 req, 1 thread)");
    unary_results.push_back(result1);

    // Test 2: Medium load unary RPC
    std::cout << "\nStarting Test 2: Medium Load Unary RPC (1000 requests, 4 threads)" << std::endl;
    auto result2 = client.testUnaryPerformance("TestUser", 25, 1000, 4);
    printUnaryTestResult(result2, "Medium Load (1000 req, 4 threads)");
    unary_results.push_back(result2);

    // Test 3: High load unary RPC
    std::cout << "\nStarting Test 3: High Load Unary RPC (5000 requests, 8 threads)" << std::endl;
    auto result3 = client.testUnaryPerformance("TestUser", 25, 5000, 8);
    printUnaryTestResult(result3, "High Load (5000 req, 8 threads)");
    unary_results.push_back(result3);

    // Test 4: Streaming RPC
    std::cout << "\nStarting Test 4: Streaming RPC (100 requests)" << std::endl;
    auto result4 = client.testStreamingPerformance("TestUser", 25, 100);
    printStreamingTestResult(result4, "Streaming (100 req)");
    streaming_results.push_back(result4);

    // Test 5: High load streaming RPC
    std::cout << "\nStarting Test 5: High Load Streaming RPC (500 requests)" << std::endl;
    auto result5 = client.testStreamingPerformance("TestUser", 25, 500);
    printStreamingTestResult(result5, "High Load Streaming (500 req)");
    streaming_results.push_back(result5);

    // Save results to file
    saveResultsToFile(unary_results, streaming_results);

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "PERFORMANCE TESTING COMPLETED" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    return 0;
} 