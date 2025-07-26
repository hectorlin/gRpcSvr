#pragma once

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <array>
#include <bitset>
#include <sched.h>
#include <netinet/tcp.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#ifdef HAVE_NUMA
#include <numa.h>
#include <linux/mempolicy.h>
#endif

namespace hello {

// Ultra-low latency optimizations
struct UltraLatencyConfig {
    static constexpr int MAX_EVENTS = 4096;  // Increased for better batching
    static constexpr int MAX_CONNECTIONS = 100000;  // Massive connection capacity
    static constexpr int BATCH_SIZE = 128;  // Larger batches for better cache efficiency
    static constexpr int NUM_WORKER_THREADS = 16;  // More threads for better parallelism
    static constexpr int EPOLL_TIMEOUT_NS = 100;  // 100ns timeout for ultra-low latency
    static constexpr int BUFFER_SIZE = 32768;  // 32KB buffers
    static constexpr int RING_BUFFER_SIZE = 128;  // Larger ring buffer
};

// Ultra-low latency memory pool with zero-copy operations
template<typename T, size_t PoolSize = 16384>
class UltraLatencyMemoryPool {
private:
    struct alignas(64) Node {
        std::atomic<Node*> next;
        T data;
    };
    
    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::array<Node, PoolSize> pool_;
    alignas(64) std::atomic<size_t> allocated_{0};
    
public:
    UltraLatencyMemoryPool() {
        // Initialize the free list with cache-friendly layout
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            pool_[i].next.store(&pool_[i + 1], std::memory_order_relaxed);
        }
        pool_[PoolSize - 1].next.store(nullptr, std::memory_order_relaxed);
        head_.store(&pool_[0], std::memory_order_relaxed);
    }
    
    T* allocate() {
        Node* old_head = head_.load(std::memory_order_acquire);
        while (old_head && !head_.compare_exchange_weak(old_head, old_head->next.load(std::memory_order_relaxed), 
                                                       std::memory_order_release, std::memory_order_acquire)) {
            // Retry on CAS failure
        }
        
        if (old_head) {
            allocated_.fetch_add(1, std::memory_order_relaxed);
            return &old_head->data;
        }
        return nullptr;
    }
    
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        Node* node = reinterpret_cast<Node*>(ptr);
        Node* old_head = head_.load(std::memory_order_acquire);
        do {
            node->next.store(old_head, std::memory_order_relaxed);
        } while (!head_.compare_exchange_weak(old_head, node, 
                                             std::memory_order_release, std::memory_order_acquire));
        
        allocated_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    size_t allocated() const { return allocated_.load(std::memory_order_relaxed); }
};

// Ultra-low latency connection with zero-copy optimizations
struct UltraLatencyConnection {
    int fd;
    std::string remote_addr;
    uint16_t remote_port;
    
    // Pre-allocated buffers with cache-line alignment
    alignas(64) std::array<uint8_t, UltraLatencyConfig::BUFFER_SIZE> read_buffer;
    alignas(64) std::array<uint8_t, UltraLatencyConfig::BUFFER_SIZE> write_buffer;
    size_t read_pos;
    size_t write_pos;
    
    // Lock-free ring buffer with larger size
    alignas(64) std::array<std::array<uint8_t, 8192>, UltraLatencyConfig::RING_BUFFER_SIZE> write_queue;
    alignas(64) std::atomic<size_t> write_head{0};
    alignas(64) std::atomic<size_t> write_tail{0};
    
    bool keep_alive;
    time_t last_activity;
    int cpu_core;
    
    // Ultra-low latency tracking
    alignas(64) std::atomic<uint64_t> request_count{0};
    alignas(64) std::atomic<uint64_t> total_latency_ns{0};
    alignas(64) std::atomic<uint64_t> min_latency_ns{UINT64_MAX};
    alignas(64) std::atomic<uint64_t> max_latency_ns{0};
    
    UltraLatencyConnection() : fd(-1), read_pos(0), write_pos(0), keep_alive(false), 
                              last_activity(0), cpu_core(-1) {
        read_buffer.fill(0);
        write_buffer.fill(0);
        for (auto& buf : write_queue) {
            buf.fill(0);
        }
    }
    
    UltraLatencyConnection(int socket_fd, int core = -1) 
        : fd(socket_fd), read_pos(0), write_pos(0), keep_alive(true), 
          last_activity(time(nullptr)), cpu_core(core) {
        read_buffer.fill(0);
        write_buffer.fill(0);
        for (auto& buf : write_queue) {
            buf.fill(0);
        }
    }
    
    // Ultra-fast lock-free operations
    bool enqueueWrite(const std::vector<uint8_t>& data) {
        size_t head = write_head.load(std::memory_order_acquire);
        size_t next_head = (head + 1) % UltraLatencyConfig::RING_BUFFER_SIZE;
        
        if (next_head == write_tail.load(std::memory_order_acquire)) {
            return false;
        }
        
        size_t data_size = std::min(data.size(), write_queue[head].size());
        std::copy(data.begin(), data.begin() + data_size, write_queue[head].begin());
        
        write_head.store(next_head, std::memory_order_release);
        return true;
    }
    
    bool dequeueWrite(std::vector<uint8_t>& data) {
        size_t tail = write_tail.load(std::memory_order_acquire);
        
        if (tail == write_head.load(std::memory_order_acquire)) {
            return false;
        }
        
        data.assign(write_queue[tail].begin(), write_queue[tail].end());
        write_tail.store((tail + 1) % UltraLatencyConfig::RING_BUFFER_SIZE, std::memory_order_release);
        return true;
    }
    
    // Record latency with minimal overhead
    void recordLatency(uint64_t latency_ns) {
        request_count.fetch_add(1, std::memory_order_relaxed);
        total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
        
        uint64_t current_min = min_latency_ns.load(std::memory_order_acquire);
        while (latency_ns < current_min && 
               !min_latency_ns.compare_exchange_weak(current_min, latency_ns, 
                                                   std::memory_order_release, std::memory_order_acquire)) {}
        
        uint64_t current_max = max_latency_ns.load(std::memory_order_acquire);
        while (latency_ns > current_max && 
               !max_latency_ns.compare_exchange_weak(current_max, latency_ns, 
                                                   std::memory_order_release, std::memory_order_acquire)) {}
    }
};

// Ultra-low latency server with maximum optimizations
class UltraLowLatencyServer {
public:
    static UltraLowLatencyServer& getInstance();
    
    UltraLowLatencyServer(const UltraLowLatencyServer&) = delete;
    UltraLowLatencyServer& operator=(const UltraLowLatencyServer&) = delete;
    
    bool startServer(const std::string& address, uint16_t port);
    void stopServer();
    bool isRunning() const;
    
    // Ultra-low latency statistics
    struct UltraLatencyStats {
        alignas(64) std::atomic<uint64_t> total_connections{0};
        alignas(64) std::atomic<uint64_t> active_connections{0};
        alignas(64) std::atomic<uint64_t> total_requests{0};
        alignas(64) std::atomic<uint64_t> total_bytes_sent{0};
        alignas(64) std::atomic<uint64_t> total_bytes_received{0};
        alignas(64) std::atomic<uint64_t> epoll_events_processed{0};
        alignas(64) std::atomic<uint64_t> lock_free_allocations{0};
        alignas(64) std::atomic<uint64_t> cache_misses{0};
        alignas(64) std::atomic<uint64_t> numa_crossings{0};
        
        // Ultra-precise latency tracking
        alignas(64) std::atomic<uint64_t> min_latency_ns{UINT64_MAX};
        alignas(64) std::atomic<uint64_t> max_latency_ns{0};
        alignas(64) std::atomic<uint64_t> total_latency_ns{0};
        alignas(64) std::atomic<uint64_t> latency_count{0};
        alignas(64) std::atomic<uint64_t> sub_microsecond_requests{0};
        alignas(64) std::atomic<uint64_t> sub_100ns_requests{0};
    };
    
    UltraLatencyStats& getStats() { return stats_; }
    
private:
    UltraLowLatencyServer() = default;
    ~UltraLowLatencyServer() = default;
    
    // Ultra-low latency optimizations
    bool setCpuAffinity(int cpu_core);
    bool setNumaAffinity(int numa_node);
    void optimizeMemoryLayout();
    void preWarmCaches();
    void setThreadPriority();
    void optimizeNetworkStack();
    
    // Ultra-fast epoll handling
    bool initializeEpoll();
    bool setNonBlocking(int fd);
    bool addToEpoll(int fd, uint32_t events);
    bool removeFromEpoll(int fd);
    void handleEpollEvents();
    
    // Ultra-fast connection management
    void acceptNewConnection();
    void handleClientData(UltraLatencyConnection* conn);
    void handleClientWrite(UltraLatencyConnection* conn);
    void closeConnection(UltraLatencyConnection* conn);
    void cleanupInactiveConnections();
    
    // Ultra-fast request processing
    void processGrpcRequest(UltraLatencyConnection* conn, const std::vector<uint8_t>& data);
    std::vector<uint8_t> createGrpcResponse(const std::string& message);
    std::string parseGrpcRequest(const std::vector<uint8_t>& data);
    
    // Pre-compiled ultra-fast responses
    std::vector<uint8_t> pre_compiled_hello_response_;
    std::vector<uint8_t> pre_compiled_error_response_;
    std::vector<uint8_t> pre_compiled_ping_response_;
    
    // Ultra-fast thread management
    void epollWorkerThread(int thread_id);
    void cleanupThread();
    
    // Server state
    int server_socket_;
    int epoll_fd_;
    std::atomic<bool> running_{false};
    std::string server_address_;
    uint16_t server_port_;
    
    // Ultra-fast thread management
    std::vector<std::thread> worker_threads_;
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    std::vector<int> cpu_cores_;
    
    // Ultra-fast connection management
    alignas(64) std::map<int, std::shared_ptr<UltraLatencyConnection>> connections_;
    alignas(64) std::mutex connections_mutex_;
    
    // Ultra-fast memory pools
    UltraLatencyMemoryPool<UltraLatencyConnection, 50000> connection_pool_;
    
    // Service instances
    std::unique_ptr<HelloServiceImpl> service_;
    
    // Ultra-precise statistics
    alignas(64) UltraLatencyStats stats_;
    
    // NUMA configuration
    int numa_node_;
    bool numa_available_;
};

} // namespace hello 