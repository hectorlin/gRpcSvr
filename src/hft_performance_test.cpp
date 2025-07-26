#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <sched.h>
#include <pthread.h>
#include <netinet/tcp.h>
#ifdef HAVE_NUMA
#include <numa.h>
#endif

// HFT-optimized performance test client
class HFTPerformanceTest {
private:
    static constexpr int NUM_THREADS = 8;
    static constexpr int REQUESTS_PER_THREAD = 10000;
    static constexpr int WARMUP_REQUESTS = 1000;
    static constexpr int CONCURRENCY_LEVELS[] = {1, 10, 50, 100, 500, 1000};
    
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> successful_requests{0};
    std::atomic<uint64_t> failed_requests{0};
    std::atomic<uint64_t> total_latency_ns{0};
    
    // High-resolution latency tracking
    std::vector<uint64_t> latency_samples;
    std::mutex latency_mutex;
    
    // Pre-compiled request templates for zero-allocation
    std::vector<uint8_t> pre_compiled_hello_request_;
    std::vector<uint8_t> pre_compiled_stream_request_;
    
public:
    HFTPerformanceTest() {
        // Pre-compile common requests
        pre_compiled_hello_request_ = createHelloRequest();
        pre_compiled_stream_request_ = createStreamRequest();
        
        // Pre-allocate latency samples vector
        latency_samples.reserve(NUM_THREADS * REQUESTS_PER_THREAD);
    }
    
    void runTest(const std::string& server_ip, int server_port) {
        std::cout << "=== HFT-Optimized Performance Test ===" << std::endl;
        std::cout << "Server: " << server_ip << ":" << server_port << std::endl;
        std::cout << "Threads: " << NUM_THREADS << std::endl;
        std::cout << "Requests per thread: " << REQUESTS_PER_THREAD << std::endl;
        std::cout << "Total requests: " << (NUM_THREADS * REQUESTS_PER_THREAD) << std::endl;
        std::cout << "=====================================" << std::endl;
        
        // Warmup phase
        std::cout << "\nWarming up..." << std::endl;
        warmup(server_ip, server_port);
        
        // Single request latency test
        std::cout << "\nSingle Request Latency Test:" << std::endl;
        testSingleRequestLatency(server_ip, server_port);
        
        // Concurrency tests
        for (int concurrency : CONCURRENCY_LEVELS) {
            std::cout << "\nConcurrency Test (" << concurrency << " concurrent requests):" << std::endl;
            testConcurrency(server_ip, server_port, concurrency);
        }
        
        // Throughput test
        std::cout << "\nThroughput Test:" << std::endl;
        testThroughput(server_ip, server_port);
        
        // Print final statistics
        printFinalStatistics();
    }
    
private:
    void warmup(const std::string& server_ip, int server_port) {
        for (int i = 0; i < WARMUP_REQUESTS; ++i) {
            int sock = createConnection(server_ip, server_port);
            if (sock >= 0) {
                sendRequest(sock, pre_compiled_hello_request_);
                close(sock);
            }
            if (i % 100 == 0) {
                std::cout << "Warmup progress: " << i << "/" << WARMUP_REQUESTS << std::endl;
            }
        }
    }
    
    void testSingleRequestLatency(const std::string& server_ip, int server_port) {
        std::vector<uint64_t> latencies;
        latencies.reserve(100);
        
        for (int i = 0; i < 100; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            int sock = createConnection(server_ip, server_port);
            if (sock >= 0) {
                bool success = sendRequest(sock, pre_compiled_hello_request_);
                close(sock);
                
                if (success) {
                    auto end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                    latencies.push_back(latency);
                }
            }
        }
        
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            uint64_t min_latency = latencies.front();
            uint64_t max_latency = latencies.back();
            uint64_t avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0ULL) / latencies.size();
            uint64_t p50_latency = latencies[latencies.size() * 50 / 100];
            uint64_t p95_latency = latencies[latencies.size() * 95 / 100];
            uint64_t p99_latency = latencies[latencies.size() * 99 / 100];
            uint64_t p999_latency = latencies[latencies.size() * 999 / 1000];
            
            std::cout << "  Min latency: " << min_latency << " ns (" << min_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  Max latency: " << max_latency << " ns (" << max_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  Avg latency: " << avg_latency << " ns (" << avg_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P50 latency: " << p50_latency << " ns (" << p50_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P95 latency: " << p95_latency << " ns (" << p95_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99 latency: " << p99_latency << " ns (" << p99_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99.9 latency: " << p999_latency << " ns (" << p999_latency / 1000.0 << " μs)" << std::endl;
        }
    }
    
    void testConcurrency(const std::string& server_ip, int server_port, int concurrency) {
        std::vector<std::thread> threads;
        std::atomic<int> active_threads{0};
        std::atomic<uint64_t> completed_requests{0};
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Start worker threads
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&, i]() {
                // Set CPU affinity for this thread
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(i % 8, &cpuset); // Bind to CPU core
                pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
                
                int requests_per_thread = REQUESTS_PER_THREAD / NUM_THREADS;
                
                for (int j = 0; j < requests_per_thread; ++j) {
                    // Wait for concurrency limit
                    while (active_threads.load() >= concurrency) {
                        std::this_thread::yield();
                    }
                    
                    active_threads.fetch_add(1);
                    
                    auto request_start = std::chrono::high_resolution_clock::now();
                    
                    int sock = createConnection(server_ip, server_port);
                    if (sock >= 0) {
                        bool success = sendRequest(sock, pre_compiled_hello_request_);
                        close(sock);
                        
                        auto request_end = std::chrono::high_resolution_clock::now();
                        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(request_end - request_start).count();
                        
                        if (success) {
                            successful_requests.fetch_add(1);
                            total_latency_ns.fetch_add(latency);
                            
                            // Store latency sample
                            {
                                std::lock_guard<std::mutex> lock(latency_mutex);
                                latency_samples.push_back(latency);
                            }
                        } else {
                            failed_requests.fetch_add(1);
                        }
                        
                        completed_requests.fetch_add(1);
                    } else {
                        failed_requests.fetch_add(1);
                        completed_requests.fetch_add(1);
                    }
                    
                    active_threads.fetch_sub(1);
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        // Calculate statistics
        uint64_t total_reqs = completed_requests.load();
        uint64_t success_reqs = successful_requests.load();
        uint64_t fail_reqs = failed_requests.load();
        double success_rate = (total_reqs > 0) ? (double)success_reqs / total_reqs * 100.0 : 0.0;
        double throughput = (total_time > 0) ? (double)total_reqs / total_time * 1000.0 : 0.0;
        
        std::cout << "  Total requests: " << total_reqs << std::endl;
        std::cout << "  Successful: " << success_reqs << std::endl;
        std::cout << "  Failed: " << fail_reqs << std::endl;
        std::cout << "  Success rate: " << success_rate << "%" << std::endl;
        std::cout << "  Throughput: " << throughput << " RPS" << std::endl;
        std::cout << "  Total time: " << total_time << " ms" << std::endl;
        
        if (!latency_samples.empty()) {
            std::sort(latency_samples.begin(), latency_samples.end());
            uint64_t avg_latency = total_latency_ns.load() / success_reqs;
            uint64_t p50_latency = latency_samples[latency_samples.size() * 50 / 100];
            uint64_t p95_latency = latency_samples[latency_samples.size() * 95 / 100];
            uint64_t p99_latency = latency_samples[latency_samples.size() * 99 / 100];
            uint64_t p999_latency = latency_samples[latency_samples.size() * 999 / 1000];
            
            std::cout << "  Avg latency: " << avg_latency << " ns (" << avg_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P50 latency: " << p50_latency << " ns (" << p50_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P95 latency: " << p95_latency << " ns (" << p95_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99 latency: " << p99_latency << " ns (" << p99_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99.9 latency: " << p999_latency << " ns (" << p999_latency / 1000.0 << " μs)" << std::endl;
        }
    }
    
    void testThroughput(const std::string& server_ip, int server_port) {
        std::cout << "Running sustained throughput test for 10 seconds..." << std::endl;
        
        std::atomic<uint64_t> requests_sent{0};
        std::atomic<uint64_t> responses_received{0};
        std::atomic<bool> stop_test{false};
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Start throughput test threads
        std::vector<std::thread> throughput_threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            throughput_threads.emplace_back([&, i]() {
                // Set CPU affinity
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(i % 8, &cpuset);
                pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
                
                while (!stop_test.load()) {
                    int sock = createConnection(server_ip, server_port);
                    if (sock >= 0) {
                        requests_sent.fetch_add(1);
                        bool success = sendRequest(sock, pre_compiled_hello_request_);
                        if (success) {
                            responses_received.fetch_add(1);
                        }
                        close(sock);
                    }
                }
            });
        }
        
        // Run for 10 seconds
        std::this_thread::sleep_for(std::chrono::seconds(10));
        stop_test.store(true);
        
        // Wait for threads to finish
        for (auto& thread : throughput_threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        uint64_t total_sent = requests_sent.load();
        uint64_t total_received = responses_received.load();
        double throughput = (total_time > 0) ? (double)total_received / total_time * 1000.0 : 0.0;
        double success_rate = (total_sent > 0) ? (double)total_received / total_sent * 100.0 : 0.0;
        
        std::cout << "  Requests sent: " << total_sent << std::endl;
        std::cout << "  Responses received: " << total_received << std::endl;
        std::cout << "  Success rate: " << success_rate << "%" << std::endl;
        std::cout << "  Sustained throughput: " << throughput << " RPS" << std::endl;
        std::cout << "  Test duration: " << total_time << " ms" << std::endl;
    }
    
    void printFinalStatistics() {
        std::cout << "\n=== Final Statistics ===" << std::endl;
        std::cout << "Total requests processed: " << total_requests.load() << std::endl;
        std::cout << "Successful requests: " << successful_requests.load() << std::endl;
        std::cout << "Failed requests: " << failed_requests.load() << std::endl;
        
        if (!latency_samples.empty()) {
            std::sort(latency_samples.begin(), latency_samples.end());
            uint64_t min_latency = latency_samples.front();
            uint64_t max_latency = latency_samples.back();
            uint64_t avg_latency = total_latency_ns.load() / successful_requests.load();
            uint64_t p50_latency = latency_samples[latency_samples.size() * 50 / 100];
            uint64_t p95_latency = latency_samples[latency_samples.size() * 95 / 100];
            uint64_t p99_latency = latency_samples[latency_samples.size() * 99 / 100];
            uint64_t p999_latency = latency_samples[latency_samples.size() * 999 / 1000];
            
            std::cout << "\nLatency Statistics:" << std::endl;
            std::cout << "  Min: " << min_latency << " ns (" << min_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  Max: " << max_latency << " ns (" << max_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  Avg: " << avg_latency << " ns (" << avg_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P50: " << p50_latency << " ns (" << p50_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P95: " << p95_latency << " ns (" << p95_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99: " << p99_latency << " ns (" << p99_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99.9: " << p999_latency << " ns (" << p999_latency / 1000.0 << " μs)" << std::endl;
        }
    }
    
    int createConnection(const std::string& server_ip, int server_port) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;
        
        // Set non-blocking mode
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        
        // Set TCP_NODELAY for low latency
        int opt = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            if (errno != EINPROGRESS) {
                close(sock);
                return -1;
            }
            
            // Wait for connection to complete
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock, &write_fds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            if (select(sock + 1, nullptr, &write_fds, nullptr, &timeout) <= 0) {
                close(sock);
                return -1;
            }
        }
        
        return sock;
    }
    
    bool sendRequest(int sock, const std::vector<uint8_t>& request) {
        ssize_t bytes_sent = send(sock, request.data(), request.size(), MSG_NOSIGNAL);
        if (bytes_sent != static_cast<ssize_t>(request.size())) {
            return false;
        }
        
        // Wait for response
        char buffer[4096];
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
        return bytes_received > 0;
    }
    
    std::vector<uint8_t> createHelloRequest() {
        // Create HTTP/2 HEADERS frame for hello request
        std::vector<uint8_t> request;
        
        // Frame header (9 bytes)
        uint32_t payload_length = 20; // Approximate header length
        request.push_back((payload_length >> 16) & 0xFF);
        request.push_back((payload_length >> 8) & 0xFF);
        request.push_back(payload_length & 0xFF);
        request.push_back(1); // HEADERS frame type
        request.push_back(0x04); // END_HEADERS flag
        request.push_back(0); // Stream ID (1)
        request.push_back(0);
        request.push_back(0);
        request.push_back(1);
        
        // HTTP/2 headers (simplified)
        std::string headers = ":method:POST\r\n:path:/hello.HelloService/SayHello\r\ncontent-type:application/grpc\r\n\r\n";
        request.insert(request.end(), headers.begin(), headers.end());
        
        return request;
    }
    
    std::vector<uint8_t> createStreamRequest() {
        // Create HTTP/2 HEADERS frame for streaming request
        std::vector<uint8_t> request;
        
        // Frame header (9 bytes)
        uint32_t payload_length = 25; // Approximate header length
        request.push_back((payload_length >> 16) & 0xFF);
        request.push_back((payload_length >> 8) & 0xFF);
        request.push_back(payload_length & 0xFF);
        request.push_back(1); // HEADERS frame type
        request.push_back(0x04); // END_HEADERS flag
        request.push_back(0); // Stream ID (3)
        request.push_back(0);
        request.push_back(0);
        request.push_back(3);
        
        // HTTP/2 headers (simplified)
        std::string headers = ":method:POST\r\n:path:/hello.HelloService/SayHelloStream\r\ncontent-type:application/grpc\r\n\r\n";
        request.insert(request.end(), headers.begin(), headers.end());
        
        return request;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <server_ip> <server_port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 127.0.0.1 50052" << std::endl;
        return 1;
    }
    
    std::string server_ip = argv[1];
    int server_port = std::stoi(argv[2]);
    
    HFTPerformanceTest test;
    test.runTest(server_ip, server_port);
    
    return 0;
} 