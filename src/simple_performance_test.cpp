#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
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

class SimplePerformanceTest {
public:
    SimplePerformanceTest(std::shared_ptr<Channel> channel)
        : stub_(HelloService::NewStub(channel)) {}

    void runBasicTest() {
        std::cout << "Running Basic Connectivity Test..." << std::endl;
        
        HelloRequest request;
        request.set_name("TestUser");
        request.set_age(25);

        HelloResponse response;
        ClientContext context;

        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub_->SayHello(&context, request, &response);
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        if (status.ok()) {
            std::cout << "✓ Basic test PASSED" << std::endl;
            std::cout << "Response: " << response.message() << std::endl;
            std::cout << "Latency: " << duration << " microseconds" << std::endl;
        } else {
            std::cout << "✗ Basic test FAILED" << std::endl;
            std::cout << "Error: " << status.error_message() << std::endl;
            std::cout << "Error Code: " << status.error_code() << std::endl;
        }
    }

    void runLatencyTest(int num_requests) {
        std::cout << "\nRunning Latency Test (" << num_requests << " requests)..." << std::endl;
        
        std::vector<double> latencies;
        int success_count = 0;
        int failure_count = 0;

        for (int i = 0; i < num_requests; ++i) {
            HelloRequest request;
            request.set_name("User_" + std::to_string(i));
            request.set_age(25 + (i % 50));

            HelloResponse response;
            ClientContext context;

            auto start = std::chrono::high_resolution_clock::now();
            Status status = stub_->SayHello(&context, request, &response);
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0; // Convert to ms
            latencies.push_back(duration);

            if (status.ok()) {
                success_count++;
            } else {
                failure_count++;
                if (i < 5) { // Show first few errors
                    std::cout << "Request " << i << " failed: " << status.error_message() << std::endl;
                }
            }
        }

        // Calculate statistics
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            
            double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
            double min_latency = latencies.front();
            double max_latency = latencies.back();
            double p50_latency = latencies[latencies.size() * 0.5];
            double p95_latency = latencies[latencies.size() * 0.95];
            double p99_latency = latencies[latencies.size() * 0.99];

            std::cout << "✓ Latency test completed" << std::endl;
            std::cout << "Success Rate: " << std::fixed << std::setprecision(2) 
                      << (success_count * 100.0 / num_requests) << "%" << std::endl;
            std::cout << "Latency Statistics (ms):" << std::endl;
            std::cout << "  Average: " << std::fixed << std::setprecision(2) << avg_latency << std::endl;
            std::cout << "  Min:     " << std::fixed << std::setprecision(2) << min_latency << std::endl;
            std::cout << "  Max:     " << std::fixed << std::setprecision(2) << max_latency << std::endl;
            std::cout << "  P50:     " << std::fixed << std::setprecision(2) << p50_latency << std::endl;
            std::cout << "  P95:     " << std::fixed << std::setprecision(2) << p95_latency << std::endl;
            std::cout << "  P99:     " << std::fixed << std::setprecision(2) << p99_latency << std::endl;
        }
    }

    void runConcurrencyTest(int num_requests, int num_threads) {
        std::cout << "\nRunning Concurrency Test (" << num_requests << " requests, " << num_threads << " threads)..." << std::endl;
        
        std::atomic<int> success_count{0};
        std::atomic<int> failure_count{0};
        std::vector<double> latencies;
        std::mutex latency_mutex;

        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        int requests_per_thread = num_requests / num_threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < requests_per_thread; ++i) {
                    HelloRequest request;
                    request.set_name("Thread_" + std::to_string(t) + "_User_" + std::to_string(i));
                    request.set_age(25 + (i % 50));

                    HelloResponse response;
                    ClientContext context;

                    auto req_start = std::chrono::high_resolution_clock::now();
                    Status status = stub_->SayHello(&context, request, &response);
                    auto req_end = std::chrono::high_resolution_clock::now();

                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(req_end - req_start).count() / 1000.0;
                    
                    {
                        std::lock_guard<std::mutex> lock(latency_mutex);
                        latencies.push_back(latency);
                    }

                    if (status.ok()) {
                        success_count++;
                    } else {
                        failure_count++;
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            
            double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
            double throughput = (success_count.load() * 1000.0) / total_duration;

            std::cout << "✓ Concurrency test completed" << std::endl;
            std::cout << "Success Rate: " << std::fixed << std::setprecision(2) 
                      << (success_count.load() * 100.0 / num_requests) << "%" << std::endl;
            std::cout << "Throughput: " << std::fixed << std::setprecision(2) << throughput << " RPS" << std::endl;
            std::cout << "Avg Latency: " << std::fixed << std::setprecision(2) << avg_latency << " ms" << std::endl;
            std::cout << "Total Duration: " << total_duration << " ms" << std::endl;
        }
    }

    void runStreamingTest(int num_requests) {
        std::cout << "\nRunning Streaming Test (" << num_requests << " requests)..." << std::endl;
        
        int success_count = 0;
        int failure_count = 0;
        int total_messages = 0;
        std::vector<double> latencies;

        for (int i = 0; i < num_requests; ++i) {
            HelloRequest request;
            request.set_name("StreamUser_" + std::to_string(i));
            request.set_age(25 + (i % 50));

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
                success_count++;
                total_messages += message_count;
            } else {
                failure_count++;
                if (i < 3) { // Show first few errors
                    std::cout << "Stream request " << i << " failed: " << status.error_message() << std::endl;
                }
            }
        }

        if (!latencies.empty()) {
            double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
            
            std::cout << "✓ Streaming test completed" << std::endl;
            std::cout << "Success Rate: " << std::fixed << std::setprecision(2) 
                      << (success_count * 100.0 / num_requests) << "%" << std::endl;
            std::cout << "Total Messages: " << total_messages << std::endl;
            std::cout << "Avg Messages/Request: " << std::fixed << std::setprecision(2) 
                      << (total_messages / (double)success_count) << std::endl;
            std::cout << "Avg Latency: " << std::fixed << std::setprecision(2) << avg_latency << " ms" << std::endl;
        }
    }

private:
    std::unique_ptr<HelloService::Stub> stub_;
};

int main() {
    std::string serverAddress = "localhost:50051";
    
    std::cout << "Simple gRPC Performance Test" << std::endl;
    std::cout << "Connecting to server at: " << serverAddress << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    SimplePerformanceTest test(channel);

    // Run tests
    test.runBasicTest();
    test.runLatencyTest(100);
    test.runConcurrencyTest(500, 4);
    test.runStreamingTest(50);

    std::cout << "\n" << std::string(50, '=') << std::endl;
    std::cout << "Performance testing completed!" << std::endl;
    
    return 0;
} 