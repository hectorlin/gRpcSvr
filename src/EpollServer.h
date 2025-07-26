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

namespace hello {

// Forward declarations
class HelloServiceImpl;
class LoggingInterceptor;

struct Connection {
    int fd;
    std::string remote_addr;
    uint16_t remote_port;
    std::vector<uint8_t> read_buffer;
    std::queue<std::vector<uint8_t>> write_queue;
    std::mutex write_mutex;
    bool keep_alive;
    time_t last_activity;
    
    Connection(int socket_fd) : fd(socket_fd), keep_alive(true), last_activity(time(nullptr)) {
        read_buffer.reserve(8192); // Pre-allocate 8KB buffer
    }
    
    ~Connection() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

class EpollServer {
public:
    static EpollServer& getInstance();
    
    // Delete copy constructor and assignment operator
    EpollServer(const EpollServer&) = delete;
    EpollServer& operator=(const EpollServer&) = delete;
    
    bool startServer(const std::string& address, uint16_t port);
    void stopServer();
    bool isRunning() const;
    
    // Performance monitoring
    struct ServerStats {
        std::atomic<uint64_t> total_connections{0};
        std::atomic<uint64_t> active_connections{0};
        std::atomic<uint64_t> total_requests{0};
        std::atomic<uint64_t> total_bytes_sent{0};
        std::atomic<uint64_t> total_bytes_received{0};
        std::atomic<uint64_t> epoll_events_processed{0};
    };
    
    ServerStats& getStats() { return stats_; }
    
private:
    EpollServer() = default;
    ~EpollServer() = default;
    
    // Epoll event handling
    bool initializeEpoll();
    bool setNonBlocking(int fd);
    bool addToEpoll(int fd, uint32_t events);
    bool removeFromEpoll(int fd);
    void handleEpollEvents();
    
    // Connection management
    void acceptNewConnection();
    void handleClientData(Connection* conn);
    void handleClientWrite(Connection* conn);
    void closeConnection(Connection* conn);
    void cleanupInactiveConnections();
    
    // HTTP/2 and gRPC handling (simplified)
    void processGrpcRequest(Connection* conn, const std::vector<uint8_t>& data);
    std::vector<uint8_t> createGrpcResponse(const std::string& message);
    std::string parseGrpcRequest(const std::vector<uint8_t>& data);
    
    // Thread management
    void epollWorkerThread();
    void cleanupThread();
    
    // Server configuration
    static constexpr int MAX_EVENTS = 1024;
    static constexpr int MAX_CONNECTIONS = 10000;
    static constexpr int CONNECTION_TIMEOUT = 300; // 5 minutes
    static constexpr int CLEANUP_INTERVAL = 60; // 1 minute
    
    // Server state
    int server_socket_;
    int epoll_fd_;
    std::atomic<bool> running_{false};
    std::string server_address_;
    uint16_t server_port_;
    
    // Thread management
    std::vector<std::thread> worker_threads_;
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    
    // Connection management - use shared_ptr for safety
    std::map<int, std::shared_ptr<Connection>> connections_;
    std::mutex connections_mutex_;
    
    // Service instances
    std::unique_ptr<HelloServiceImpl> service_;
    
    // Statistics
    ServerStats stats_;
    
    // Thread pool configuration
    static constexpr int NUM_WORKER_THREADS = 4;
};

} // namespace hello 