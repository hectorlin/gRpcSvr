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
#ifdef HAVE_NUMA
#include <numa.h>
#include <linux/mempolicy.h>
#endif

namespace hello {

// Forward declarations
class HelloServiceImpl;
class LoggingInterceptor;

// HFT-optimized memory pool for zero-allocation operations
template<typename T, size_t PoolSize = 1024>
class LockFreeMemoryPool {
private:
    struct Node {
        std::atomic<Node*> next;
        T data;
    };
    
    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::array<Node, PoolSize> pool_;
    alignas(64) std::atomic<size_t> allocated_{0};
    
public:
    LockFreeMemoryPool() {
        // Initialize the free list
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
        return nullptr; // Pool exhausted
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

// HFT-optimized connection with pre-allocated buffers and lock-free operations
struct Connection {
    int fd;
    std::string remote_addr;
    uint16_t remote_port;
    
    // Pre-allocated buffers for zero-allocation operations
    alignas(64) std::array<uint8_t, 16384> read_buffer;  // 16KB pre-allocated
    alignas(64) std::array<uint8_t, 16384> write_buffer; // 16KB pre-allocated
    size_t read_pos;
    size_t write_pos;
    
    // Lock-free write queue using ring buffer
    static constexpr size_t RING_BUFFER_SIZE = 64;
    alignas(64) std::array<std::array<uint8_t, 4096>, RING_BUFFER_SIZE> write_queue;
    alignas(64) std::atomic<size_t> write_head{0};
    alignas(64) std::atomic<size_t> write_tail{0};
    
    bool keep_alive;
    time_t last_activity;
    
    // CPU core affinity for this connection
    int cpu_core;
    
    Connection() : fd(-1), read_pos(0), write_pos(0), keep_alive(false), 
                   last_activity(0), cpu_core(-1) {
        // Zero-initialize buffers
        read_buffer.fill(0);
        write_buffer.fill(0);
        for (auto& buf : write_queue) {
            buf.fill(0);
        }
    }
    
    Connection(int socket_fd, int core = -1) 
        : fd(socket_fd), read_pos(0), write_pos(0), keep_alive(true), 
          last_activity(time(nullptr)), cpu_core(core) {
        // Zero-initialize buffers
        read_buffer.fill(0);
        write_buffer.fill(0);
        for (auto& buf : write_queue) {
            buf.fill(0);
        }
    }
    
    ~Connection() {
        if (fd >= 0) {
            close(fd);
        }
    }
    
    // Lock-free write queue operations
    bool enqueueWrite(const std::vector<uint8_t>& data) {
        size_t head = write_head.load(std::memory_order_acquire);
        size_t next_head = (head + 1) % RING_BUFFER_SIZE;
        
        if (next_head == write_tail.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        size_t data_size = std::min(data.size(), write_queue[head].size());
        std::copy(data.begin(), data.begin() + data_size, write_queue[head].begin());
        
        write_head.store(next_head, std::memory_order_release);
        return true;
    }
    
    bool dequeueWrite(std::vector<uint8_t>& data) {
        size_t tail = write_tail.load(std::memory_order_acquire);
        
        if (tail == write_head.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        
        data.assign(write_queue[tail].begin(), write_queue[tail].end());
        write_tail.store((tail + 1) % RING_BUFFER_SIZE, std::memory_order_release);
        return true;
    }
};

// HFT-optimized server with lock-free operations and CPU affinity
class EpollServer {
public:
    static EpollServer& getInstance();
    
    // Delete copy constructor and assignment operator
    EpollServer(const EpollServer&) = delete;
    EpollServer& operator=(const EpollServer&) = delete;
    
    bool startServer(const std::string& address, uint16_t port);
    void stopServer();
    bool isRunning() const;
    
    // Performance monitoring with high-resolution timestamps
    struct ServerStats {
        alignas(64) std::atomic<uint64_t> total_connections{0};
        alignas(64) std::atomic<uint64_t> active_connections{0};
        alignas(64) std::atomic<uint64_t> total_requests{0};
        alignas(64) std::atomic<uint64_t> total_bytes_sent{0};
        alignas(64) std::atomic<uint64_t> total_bytes_received{0};
        alignas(64) std::atomic<uint64_t> epoll_events_processed{0};
        alignas(64) std::atomic<uint64_t> lock_free_allocations{0};
        alignas(64) std::atomic<uint64_t> cache_misses{0};
        alignas(64) std::atomic<uint64_t> numa_crossings{0};
        
        // Latency tracking with nanosecond precision
        alignas(64) std::atomic<uint64_t> min_latency_ns{UINT64_MAX};
        alignas(64) std::atomic<uint64_t> max_latency_ns{0};
        alignas(64) std::atomic<uint64_t> total_latency_ns{0};
        alignas(64) std::atomic<uint64_t> latency_count{0};
    };
    
    ServerStats& getStats() { return stats_; }
    
private:
    EpollServer() = default;
    ~EpollServer() = default;
    
    // HFT-specific optimizations
    bool setCpuAffinity(int cpu_core);
    bool setNumaAffinity(int numa_node);
    void optimizeMemoryLayout();
    void preWarmCaches();
    
    // Epoll event handling with batch processing
    bool initializeEpoll();
    bool setNonBlocking(int fd);
    bool addToEpoll(int fd, uint32_t events);
    bool removeFromEpoll(int fd);
    void handleEpollEvents();
    
    // Connection management with lock-free operations
    void acceptNewConnection();
    void handleClientData(Connection* conn);
    void handleClientWrite(Connection* conn);
    void closeConnection(Connection* conn);
    void cleanupInactiveConnections();
    
    // HTTP/2 and gRPC handling with pre-compiled responses
    void processGrpcRequest(Connection* conn, const std::vector<uint8_t>& data);
    std::vector<uint8_t> createGrpcResponse(const std::string& message);
    std::string parseGrpcRequest(const std::vector<uint8_t>& data);
    
    // Pre-compiled response templates for common requests
    std::vector<uint8_t> pre_compiled_hello_response_;
    std::vector<uint8_t> pre_compiled_error_response_;
    
    // Thread management with CPU affinity
    void epollWorkerThread(int thread_id);
    void cleanupThread();
    
    // Server configuration optimized for HFT
    static constexpr int MAX_EVENTS = 2048;  // Increased for batch processing
    static constexpr int MAX_CONNECTIONS = 50000;  // Increased capacity
    static constexpr int CONNECTION_TIMEOUT = 300; // 5 minutes
    static constexpr int CLEANUP_INTERVAL = 60; // 1 minute
    static constexpr int BATCH_SIZE = 64;  // Process events in batches
    
    // Server state
    int server_socket_;
    int epoll_fd_;
    std::atomic<bool> running_{false};
    std::string server_address_;
    uint16_t server_port_;
    
    // Thread management with CPU affinity
    std::vector<std::thread> worker_threads_;
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    std::vector<int> cpu_cores_;  // CPU cores for worker threads
    
    // Lock-free connection management
    alignas(64) std::map<int, std::shared_ptr<Connection>> connections_;
    alignas(64) std::mutex connections_mutex_;
    
    // Memory pools for zero-allocation operations
    LockFreeMemoryPool<Connection, 10000> connection_pool_;
    
    // Service instances
    std::unique_ptr<HelloServiceImpl> service_;
    
    // Statistics with cache-line alignment
    alignas(64) ServerStats stats_;
    
    // Thread pool configuration optimized for HFT
    static constexpr int NUM_WORKER_THREADS = 8;  // Increased for better parallelism
    
    // NUMA configuration
    int numa_node_;
    bool numa_available_;
};

} // namespace hello 