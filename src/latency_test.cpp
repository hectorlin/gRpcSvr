#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>

#include "HelloService.grpc.pb.h"
#include <grpcpp/grpcpp.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using hello::HelloService;
using hello::HelloRequest;
using hello::HelloResponse;

class LatencyTestClient {
private:
    std::unique_ptr<HelloService::Stub> stub_;
    std::string serverAddress_;
    
public:
    LatencyTestClient(const std::string& address) 
        : serverAddress_(address) {
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = HelloService::NewStub(channel);
    }
    
    // Single request latency test
    double measureSingleLatency(const std::string& name, int age) {
        HelloRequest request;
        request.set_name(name);
        request.set_age(age);
        
        HelloResponse response;
        ClientContext context;
        
        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub_->SayHello(&context, request, &response);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (!status.ok()) {
            return -1.0; // Error
        }
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count() / 1000.0; // Convert to milliseconds
    }
    
    // Warmup function
    void warmup(int iterations = 10) {
        std::cout << "Warming up server with " << iterations << " requests..." << std::endl;
        for (int i = 0; i < iterations; ++i) {
            measureSingleLatency("WarmupUser_" + std::to_string(i), 25);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "Warmup completed." << std::endl;
    }
    
    // Comprehensive latency test
    void runLatencyTest(int numRequests = 1000, int numThreads = 4) {
        std::cout << "\n==========================================" << std::endl;
        std::cout << "LATENCY PERFORMANCE TEST" << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Server: " << serverAddress_ << std::endl;
        std::cout << "Requests: " << numRequests << std::endl;
        std::cout << "Threads: " << numThreads << std::endl;
        std::cout << "==========================================" << std::endl;
        
        // Warmup
        warmup();
        
        std::vector<double> latencies;
        latencies.reserve(numRequests);
        std::atomic<int> completedRequests{0};
        std::atomic<int> failedRequests{0};
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Create threads for concurrent testing
        std::vector<std::thread> threads;
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = t; i < numRequests; i += numThreads) {
                    std::string name = "TestUser_" + std::to_string(i);
                    double latency = measureSingleLatency(name, 25 + (i % 50));
                    
                    if (latency >= 0) {
                        latencies.push_back(latency);
                        completedRequests++;
                    } else {
                        failedRequests++;
                    }
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        // Calculate statistics
        if (latencies.empty()) {
            std::cout << "No successful requests completed!" << std::endl;
            return;
        }
        
        std::sort(latencies.begin(), latencies.end());
        
        double avgLatency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        double minLatency = latencies.front();
        double maxLatency = latencies.back();
        double p50Latency = latencies[latencies.size() * 0.5];
        double p95Latency = latencies[latencies.size() * 0.95];
        double p99Latency = latencies[latencies.size() * 0.99];
        double p999Latency = latencies[latencies.size() * 0.999];
        
        double throughput = (completedRequests * 1000.0) / totalDuration.count(); // RPS
        
        // Print results
        std::cout << "\n📊 LATENCY TEST RESULTS" << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Total Duration: " << totalDuration.count() << " ms" << std::endl;
        std::cout << "Completed Requests: " << completedRequests << std::endl;
        std::cout << "Failed Requests: " << failedRequests << std::endl;
        std::cout << "Success Rate: " << std::fixed << std::setprecision(2) 
                  << (completedRequests * 100.0 / numRequests) << "%" << std::endl;
        std::cout << "Throughput: " << std::fixed << std::setprecision(2) << throughput << " RPS" << std::endl;
        
        std::cout << "\n⏱️  LATENCY STATISTICS (ms)" << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Average: " << std::fixed << std::setprecision(3) << avgLatency << std::endl;
        std::cout << "Minimum: " << std::fixed << std::setprecision(3) << minLatency << std::endl;
        std::cout << "Maximum: " << std::fixed << std::setprecision(3) << maxLatency << std::endl;
        std::cout << "P50:     " << std::fixed << std::setprecision(3) << p50Latency << std::endl;
        std::cout << "P95:     " << std::fixed << std::setprecision(3) << p95Latency << std::endl;
        std::cout << "P99:     " << std::fixed << std::setprecision(3) << p99Latency << std::endl;
        std::cout << "P99.9:   " << std::fixed << std::setprecision(3) << p999Latency << std::endl;
        
        // Performance assessment
        std::cout << "\n🏆 PERFORMANCE ASSESSMENT" << std::endl;
        std::cout << "==========================================" << std::endl;
        if (avgLatency < 1.0) {
            std::cout << "✅ EXCELLENT: Average latency < 1ms" << std::endl;
        } else if (avgLatency < 5.0) {
            std::cout << "✅ GOOD: Average latency < 5ms" << std::endl;
        } else if (avgLatency < 10.0) {
            std::cout << "⚠️  ACCEPTABLE: Average latency < 10ms" << std::endl;
        } else {
            std::cout << "❌ POOR: Average latency >= 10ms" << std::endl;
        }
        
        if (throughput > 1000) {
            std::cout << "✅ EXCELLENT: Throughput > 1000 RPS" << std::endl;
        } else if (throughput > 500) {
            std::cout << "✅ GOOD: Throughput > 500 RPS" << std::endl;
        } else if (throughput > 100) {
            std::cout << "⚠️  ACCEPTABLE: Throughput > 100 RPS" << std::endl;
        } else {
            std::cout << "❌ POOR: Throughput <= 100 RPS" << std::endl;
        }
        
        // Save detailed results to file
        saveDetailedResults(latencies, avgLatency, throughput, completedRequests, failedRequests);
    }
    
private:
    void saveDetailedResults(const std::vector<double>& latencies, double avgLatency, 
                           double throughput, int completed, int failed) {
        std::ofstream file("latency_detailed_report.txt");
        if (file.is_open()) {
            file << "Detailed Latency Test Report" << std::endl;
            file << "============================" << std::endl;
            file << "Timestamp: " << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
            file << "Server: " << serverAddress_ << std::endl;
            file << "Average Latency: " << std::fixed << std::setprecision(3) << avgLatency << " ms" << std::endl;
            file << "Throughput: " << std::fixed << std::setprecision(2) << throughput << " RPS" << std::endl;
            file << "Completed Requests: " << completed << std::endl;
            file << "Failed Requests: " << failed << std::endl;
            file << "\nIndividual Latency Measurements (ms):" << std::endl;
            file << "=====================================" << std::endl;
            
            for (size_t i = 0; i < latencies.size(); ++i) {
                file << std::fixed << std::setprecision(3) << latencies[i];
                if ((i + 1) % 10 == 0) file << std::endl;
                else file << ", ";
            }
            file << std::endl;
            file.close();
            
            std::cout << "\n📄 Detailed report saved to: latency_detailed_report.txt" << std::endl;
        }
    }
};

int main() {
    std::cout << "🚀 gRPC Server Latency Test" << std::endl;
    std::cout << "===========================" << std::endl;
    
    const std::string serverAddress = "localhost:50051";
    
    try {
        LatencyTestClient client(serverAddress);
        
        // Run different latency tests
        std::cout << "\n🔍 Running comprehensive latency tests..." << std::endl;
        
        // Test 1: Light load (100 requests, 1 thread)
        std::cout << "\n📈 Test 1: Light Load (100 requests, 1 thread)" << std::endl;
        client.runLatencyTest(100, 1);
        
        // Test 2: Medium load (1000 requests, 4 threads)
        std::cout << "\n📈 Test 2: Medium Load (1000 requests, 4 threads)" << std::endl;
        client.runLatencyTest(1000, 4);
        
        // Test 3: High load (5000 requests, 8 threads)
        std::cout << "\n📈 Test 3: High Load (5000 requests, 8 threads)" << std::endl;
        client.runLatencyTest(5000, 8);
        
        std::cout << "\n✅ All latency tests completed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 