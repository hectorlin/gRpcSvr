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
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

class EpollPerformanceTest {
private:
    std::string serverAddress_;
    uint16_t serverPort_;
    
public:
    EpollPerformanceTest(const std::string& address, uint16_t port) 
        : serverAddress_(address), serverPort_(port) {}
    
    // Single request latency test
    double measureSingleLatency() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return -1.0;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(serverPort_);
        server_addr.sin_addr.s_addr = inet_addr(serverAddress_.c_str());
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            return -1.0;
        }
        
        // Create simple HTTP/2 HEADERS frame
        std::vector<uint8_t> request = createHttp2HeadersFrame();
        
        auto start = std::chrono::high_resolution_clock::now();
        
        ssize_t sent = send(sock, request.data(), request.size(), 0);
        if (sent < 0) {
            close(sock);
            return -1.0;
        }
        
        // Read response
        char buffer[4096];
        ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
        
        auto end = std::chrono::high_resolution_clock::now();
        
        close(sock);
        
        if (received < 0) {
            return -1.0;
        }
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count() / 1000.0; // Convert to milliseconds
    }
    
    // Warmup function
    void warmup(int iterations = 10) {
        std::cout << "Warming up epoll server with " << iterations << " requests..." << std::endl;
        for (int i = 0; i < iterations; ++i) {
            measureSingleLatency();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "Warmup completed." << std::endl;
    }
    
    // Comprehensive performance test
    void runPerformanceTest(int numRequests = 1000, int numThreads = 4) {
        std::cout << "\n==========================================" << std::endl;
        std::cout << "EPOLL SERVER PERFORMANCE TEST" << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Server: " << serverAddress_ << ":" << serverPort_ << std::endl;
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
                    double latency = measureSingleLatency();
                    
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
        std::cout << "\n📊 EPOLL SERVER TEST RESULTS" << std::endl;
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
        if (avgLatency < 0.5) {
            std::cout << "✅ EXCELLENT: Average latency < 0.5ms" << std::endl;
        } else if (avgLatency < 1.0) {
            std::cout << "✅ GOOD: Average latency < 1ms" << std::endl;
        } else if (avgLatency < 2.0) {
            std::cout << "⚠️  ACCEPTABLE: Average latency < 2ms" << std::endl;
        } else {
            std::cout << "❌ POOR: Average latency >= 2ms" << std::endl;
        }
        
        if (throughput > 5000) {
            std::cout << "✅ EXCELLENT: Throughput > 5000 RPS" << std::endl;
        } else if (throughput > 2000) {
            std::cout << "✅ GOOD: Throughput > 2000 RPS" << std::endl;
        } else if (throughput > 1000) {
            std::cout << "⚠️  ACCEPTABLE: Throughput > 1000 RPS" << std::endl;
        } else {
            std::cout << "❌ POOR: Throughput <= 1000 RPS" << std::endl;
        }
        
        // Save detailed results
        saveDetailedResults(latencies, avgLatency, throughput, completedRequests, failedRequests);
    }
    
private:
    std::vector<uint8_t> createHttp2HeadersFrame() {
        std::vector<uint8_t> frame;
        
        // HTTP/2 HEADERS frame (simplified)
        // Frame header (9 bytes)
        uint32_t payload_length = 32; // Approximate headers length
        frame.push_back((payload_length >> 16) & 0xFF);
        frame.push_back((payload_length >> 8) & 0xFF);
        frame.push_back(payload_length & 0xFF);
        frame.push_back(1); // HEADERS frame type
        frame.push_back(0x04); // END_HEADERS flag
        frame.push_back(0); // Stream ID (1)
        frame.push_back(0);
        frame.push_back(0);
        frame.push_back(1);
        
        // Headers payload (simplified)
        std::string headers = ":method: POST\r\n:path: /hello.HelloService/SayHello\r\n";
        frame.insert(frame.end(), headers.begin(), headers.end());
        
        return frame;
    }
    
    void saveDetailedResults(const std::vector<double>& latencies, double avgLatency, 
                           double throughput, int completed, int failed) {
        std::ofstream file("epoll_performance_report.txt");
        if (file.is_open()) {
            file << "Epoll Server Performance Test Report" << std::endl;
            file << "====================================" << std::endl;
            file << "Timestamp: " << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
            file << "Server: " << serverAddress_ << ":" << serverPort_ << std::endl;
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
            
            std::cout << "\n📄 Detailed report saved to: epoll_performance_report.txt" << std::endl;
        }
    }
};

int main() {
    std::cout << "🚀 Epoll Server Performance Test" << std::endl;
    std::cout << "================================" << std::endl;
    
    const std::string serverAddress = "127.0.0.1";
    const uint16_t serverPort = 50052;
    
    try {
        EpollPerformanceTest test(serverAddress, serverPort);
        
        // Run different performance tests
        std::cout << "\n🔍 Running epoll server performance tests..." << std::endl;
        
        // Test 1: Light load (100 requests, 1 thread)
        std::cout << "\n📈 Test 1: Light Load (100 requests, 1 thread)" << std::endl;
        test.runPerformanceTest(100, 1);
        
        // Test 2: Medium load (1000 requests, 4 threads)
        std::cout << "\n📈 Test 2: Medium Load (1000 requests, 4 threads)" << std::endl;
        test.runPerformanceTest(1000, 4);
        
        // Test 3: High load (5000 requests, 8 threads)
        std::cout << "\n📈 Test 3: High Load (5000 requests, 8 threads)" << std::endl;
        test.runPerformanceTest(5000, 8);
        
        std::cout << "\n✅ All epoll server performance tests completed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 