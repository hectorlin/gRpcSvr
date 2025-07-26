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
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#ifdef HAVE_NUMA
#include <numa.h>
#endif

// Ultra-low latency test client
class UltraLatencyTest {
private:
    static constexpr int NUM_THREADS = 16;
    static constexpr int REQUESTS_PER_THREAD = 50000;
    static constexpr int WARMUP_REQUESTS = 5000;
    static constexpr int CONCURRENCY_LEVELS[] = {1, 5, 10, 25, 50, 100, 250, 500, 1000};
    
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> successful_requests{0};
    std::atomic<uint64_t> failed_requests{0};
    std::atomic<uint64_t> total_latency_ns{0};
    
    // Ultra-precise latency tracking
    std::vector<uint64_t> latency_samples;
    std::mutex latency_mutex;
    
    // Pre-compiled ultra-fast request templates
    std::vector<uint8_t> pre_compiled_hello_request_;
    std::vector<uint8_t> pre_compiled_ping_request_;
    
    // Performance monitoring
    int perf_fd_;
    
public:
    UltraLatencyTest() {
        // Pre-compile ultra-fast requests
        pre_compiled_hello_request_ = createHelloRequest();
        pre_compiled_ping_request_ = createPingRequest();
        
        // Pre-allocate latency samples vector
        latency_samples.reserve(NUM_THREADS * REQUESTS_PER_THREAD);
        
        // Initialize performance monitoring
        initPerformanceMonitoring();
    }
    
    ~UltraLatencyTest() {
        if (perf_fd_ >= 0) {
            close(perf_fd_);
        }
    }
    
    void runTest(const std::string& server_ip, int server_port) {
        std::cout << "=== Ultra-Low Latency Performance Test ===" << std::endl;
        std::cout << "Server: " << server_ip << ":" << server_port << std::endl;
        std::cout << "Threads: " << NUM_THREADS << std::endl;
        std::cout << "Requests per thread: " << REQUESTS_PER_THREAD << std::endl;
        std::cout << "Total requests: " << (NUM_THREADS * REQUESTS_PER_THREAD) << std::endl;
        std::cout << "==========================================" << std::endl;
        
        // Warmup phase
        std::cout << "\nWarming up..." << std::endl;
        warmup(server_ip, server_port);
        
        // Ultra-precise single request latency test
        std::cout << "\nUltra-Precise Single Request Latency Test:" << std::endl;
        testUltraPreciseLatency(server_ip, server_port);
        
        // Concurrency tests with ultra-low latency focus
        for (int concurrency : CONCURRENCY_LEVELS) {
            std::cout << "\nConcurrency Test (" << concurrency << " concurrent requests):" << std::endl;
            testConcurrency(server_ip, server_port, concurrency);
        }
        
        // Ultra-low latency throughput test
        std::cout << "\nUltra-Low Latency Throughput Test:" << std::endl;
        testUltraLatencyThroughput(server_ip, server_port);
        
        // Print ultra-detailed statistics
        printUltraDetailedStatistics();
    }
    
private:
    void initPerformanceMonitoring() {
        // Initialize performance monitoring for cache misses and CPU cycles
        struct perf_event_attr pe;
        memset(&pe, 0, sizeof(struct perf_event_attr));
        pe.type = PERF_TYPE_HARDWARE;
        pe.size = sizeof(struct perf_event_attr);
        pe.config = PERF_COUNT_HW_CACHE_MISSES;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;
        
        perf_fd_ = syscall(__NR_perf_event_open, &pe, -1, 0, -1, 0);
        if (perf_fd_ < 0) {
            std::cerr << "Warning: Could not initialize performance monitoring" << std::endl;
        }
    }
    
    void warmup(const std::string& server_ip, int server_port) {
        for (int i = 0; i < WARMUP_REQUESTS; ++i) {
            int sock = createUltraOptimizedConnection(server_ip, server_port);
            if (sock >= 0) {
                sendUltraFastRequest(sock, pre_compiled_hello_request_);
                close(sock);
            }
            if (i % 500 == 0) {
                std::cout << "Warmup progress: " << i << "/" << WARMUP_REQUESTS << std::endl;
            }
        }
    }
    
    void testUltraPreciseLatency(const std::string& server_ip, int server_port) {
        std::vector<uint64_t> latencies;
        latencies.reserve(1000);
        
        for (int i = 0; i < 1000; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            int sock = createUltraOptimizedConnection(server_ip, server_port);
            if (sock >= 0) {
                bool success = sendUltraFastRequest(sock, pre_compiled_hello_request_);
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
            
            // Calculate ultra-precise statistics
            uint64_t min_latency = latencies.front();
            uint64_t max_latency = latencies.back();
            uint64_t avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0ULL) / latencies.size();
            
            // Calculate percentiles with higher precision
            uint64_t p50_latency = latencies[latencies.size() * 50 / 100];
            uint64_t p90_latency = latencies[latencies.size() * 90 / 100];
            uint64_t p95_latency = latencies[latencies.size() * 95 / 100];
            uint64_t p99_latency = latencies[latencies.size() * 99 / 100];
            uint64_t p999_latency = latencies[latencies.size() * 999 / 1000];
            uint64_t p9999_latency = latencies[latencies.size() * 9999 / 10000];
            
            // Count sub-microsecond and sub-100ns requests
            uint64_t sub_microsecond = std::count_if(latencies.begin(), latencies.end(), 
                                                   [](uint64_t l) { return l < 1000; });
            uint64_t sub_100ns = std::count_if(latencies.begin(), latencies.end(), 
                                             [](uint64_t l) { return l < 100; });
            
            std::cout << "  Min latency: " << min_latency << " ns (" << min_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  Max latency: " << max_latency << " ns (" << max_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  Avg latency: " << avg_latency << " ns (" << avg_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P50 latency: " << p50_latency << " ns (" << p50_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P90 latency: " << p90_latency << " ns (" << p90_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P95 latency: " << p95_latency << " ns (" << p95_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99 latency: " << p99_latency << " ns (" << p99_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99.9 latency: " << p999_latency << " ns (" << p999_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  P99.99 latency: " << p9999_latency << " ns (" << p9999_latency / 1000.0 << " μs)" << std::endl;
            std::cout << "  Sub-microsecond requests: " << sub_microsecond << " (" << (sub_microsecond * 100.0 / latencies.size()) << "%)" << std::endl;
            std::cout << "  Sub-100ns requests: " << sub_100ns << " (" << (sub_100ns * 100.0 / latencies.size()) << "%)" << std::endl;
        }
    }
    
    void testConcurrency(const std::string& server_ip, int server_port, int concurrency) {
        std::vector<std::thread> threads;
        std::atomic<int> active_threads{0};
        std::atomic<uint64_t> completed_requests{0};
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Start worker threads with ultra-low latency optimizations
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&, i]() {
                // Set ultra-high priority and CPU affinity
                setUltraHighPriority();
                setCpuAffinity(i % 16);
                
                int requests_per_thread = REQUESTS_PER_THREAD / NUM_THREADS;
                
                for (int j = 0; j < requests_per_thread; ++j) {
                    // Wait for concurrency limit
                    while (active_threads.load() >= concurrency) {
                        std::this_thread::yield();
                    }
                    
                    active_threads.fetch_add(1);
                    
                    auto request_start = std::chrono::high_resolution_clock::now();
                    
                    int sock = createUltraOptimizedConnection(server_ip, server_port);
                    if (sock >= 0) {
                        bool success = sendUltraFastRequest(sock, pre_compiled_hello_request_);
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
        auto total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        
        // Calculate ultra-precise statistics
        uint64_t total_reqs = completed_requests.load();
        uint64_t success_reqs = successful_requests.load();
        uint64_t fail_reqs = failed_requests.load();
        double success_rate = (total_reqs > 0) ? (double)success_reqs / total_reqs * 100.0 : 0.0;
        double throughput = (total_time > 0) ? (double)total_reqs * 1000000000.0 / total_time : 0.0;
        
        std::cout << "  Total requests: " << total_reqs << std::endl;
        std::cout << "  Successful: " << success_reqs << std::endl;
        std::cout << "  Failed: " << fail_reqs << std::endl;
        std::cout << "  Success rate: " << success_rate << "%" << std::endl;
        std::cout << "  Throughput: " << throughput << " RPS" << std::endl;
        std::cout << "  Total time: " << total_time / 1000000.0 << " ms" << std::endl;
        
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
    
    void testUltraLatencyThroughput(const std::string& server_ip, int server_port) {
        std::cout << "Running ultra-low latency throughput test for 30 seconds..." << std::endl;
        
        std::atomic<uint64_t> requests_sent{0};
        std::atomic<uint64_t> responses_received{0};
        std::atomic<bool> stop_test{false};
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Start ultra-fast throughput test threads
        std::vector<std::thread> throughput_threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            throughput_threads.emplace_back([&, i]() {
                setUltraHighPriority();
                setCpuAffinity(i % 16);
                
                while (!stop_test.load()) {
                    int sock = createUltraOptimizedConnection(server_ip, server_port);
                    if (sock >= 0) {
                        requests_sent.fetch_add(1);
                        bool success = sendUltraFastRequest(sock, pre_compiled_hello_request_);
                        if (success) {
                            responses_received.fetch_add(1);
                        }
                        close(sock);
                    }
                }
            });
        }
        
        // Run for 30 seconds
        std::this_thread::sleep_for(std::chrono::seconds(30));
        stop_test.store(true);
        
        // Wait for threads to finish
        for (auto& thread : throughput_threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        
        uint64_t total_sent = requests_sent.load();
        uint64_t total_received = responses_received.load();
        double throughput = (total_time > 0) ? (double)total_received * 1000000000.0 / total_time : 0.0;
        double success_rate = (total_sent > 0) ? (double)total_received / total_sent * 100.0 : 0.0;
        
        std::cout << "  Requests sent: " << total_sent << std::endl;
        std::cout << "  Responses received: " << total_received << std::endl;
        std::cout << "  Success rate: " << success_rate << "%" << std::endl;
        std::cout << "  Ultra-low latency throughput: " << throughput << " RPS" << std::endl;
        std::cout << "  Test duration: " << total_time / 1000000000.0 << " seconds" << std::endl;
    }
    
    void printUltraDetailedStatistics() {
        std::cout << "\n=== Ultra-Detailed Statistics ===" << std::endl;
        std::cout << "Total requests processed: " << total_requests.load() << std::endl;
        std::cout << "Successful requests: " << successful_requests.load() << std::endl;
        std::cout << "Failed requests: " << failed_requests.load() << std::endl;
        
        if (!latency_samples.empty()) {
            std::sort(latency_samples.begin(), latency_samples.end());
            
            // Calculate ultra-detailed percentiles
            std::vector<double> percentiles = {0.1, 0.5, 1.0, 5.0, 10.0, 25.0, 50.0, 75.0, 90.0, 95.0, 99.0, 99.5, 99.9, 99.95, 99.99};
            
            std::cout << "\nUltra-Detailed Latency Percentiles:" << std::endl;
            for (double p : percentiles) {
                size_t index = static_cast<size_t>(latency_samples.size() * p / 100.0);
                if (index < latency_samples.size()) {
                    uint64_t latency = latency_samples[index];
                    std::cout << "  P" << p << ": " << latency << " ns (" << latency / 1000.0 << " μs)" << std::endl;
                }
            }
            
            // Count requests in different latency ranges
            uint64_t sub_100ns = std::count_if(latency_samples.begin(), latency_samples.end(), 
                                             [](uint64_t l) { return l < 100; });
            uint64_t sub_1us = std::count_if(latency_samples.begin(), latency_samples.end(), 
                                           [](uint64_t l) { return l < 1000; });
            uint64_t sub_10us = std::count_if(latency_samples.begin(), latency_samples.end(), 
                                            [](uint64_t l) { return l < 10000; });
            uint64_t sub_100us = std::count_if(latency_samples.begin(), latency_samples.end(), 
                                             [](uint64_t l) { return l < 100000; });
            
            std::cout << "\nLatency Distribution:" << std::endl;
            std::cout << "  < 100ns: " << sub_100ns << " (" << (sub_100ns * 100.0 / latency_samples.size()) << "%)" << std::endl;
            std::cout << "  < 1μs: " << sub_1us << " (" << (sub_1us * 100.0 / latency_samples.size()) << "%)" << std::endl;
            std::cout << "  < 10μs: " << sub_10us << " (" << (sub_10us * 100.0 / latency_samples.size()) << "%)" << std::endl;
            std::cout << "  < 100μs: " << sub_100us << " (" << (sub_100us * 100.0 / latency_samples.size()) << "%)" << std::endl;
        }
    }
    
    void setUltraHighPriority() {
        // Set thread to real-time priority
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    }
    
    void setCpuAffinity(int cpu_core) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
    
    int createUltraOptimizedConnection(const std::string& server_ip, int server_port) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;
        
        // Ultra-optimized socket settings
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &opt, sizeof(opt));
        
        // Set non-blocking mode
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        
        // Ultra-large socket buffers
        int send_buf_size = 2 * 1024 * 1024; // 2MB
        int recv_buf_size = 2 * 1024 * 1024; // 2MB
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));
        
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
            
            // Ultra-fast connection wait
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock, &write_fds);
            
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 1000; // 1ms timeout
            
            if (select(sock + 1, nullptr, &write_fds, nullptr, &timeout) <= 0) {
                close(sock);
                return -1;
            }
        }
        
        return sock;
    }
    
    bool sendUltraFastRequest(int sock, const std::vector<uint8_t>& request) {
        ssize_t bytes_sent = send(sock, request.data(), request.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (bytes_sent != static_cast<ssize_t>(request.size())) {
            return false;
        }
        
        // Ultra-fast response wait
        char buffer[8192];
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
        return bytes_received > 0;
    }
    
    std::vector<uint8_t> createHelloRequest() {
        // Ultra-optimized HTTP/2 HEADERS frame
        std::vector<uint8_t> request = {
            0x00, 0x00, 0x14, // Length: 20 bytes
            0x01, // Type: HEADERS
            0x04, // Flags: END_HEADERS
            0x00, 0x00, 0x00, 0x01, // Stream ID: 1
            // HTTP/2 headers (minimal)
            ':','m','e','t','h','o','d',':','P','O','S','T','\r','\n',
            ':','p','a','t','h',':','/','h','e','l','l','o','\r','\n'
        };
        return request;
    }
    
    std::vector<uint8_t> createPingRequest() {
        // Ultra-optimized ping request
        std::vector<uint8_t> request = {
            0x00, 0x00, 0x08, // Length: 8 bytes
            0x06, // Type: PING
            0x00, // Flags: none
            0x00, 0x00, 0x00, 0x00, // Stream ID: 0
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // Ping payload
        };
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
    
    UltraLatencyTest test;
    test.runTest(server_ip, server_port);
    
    return 0;
} 