#include "EpollServer.h"
#include "HelloService.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <sys/sysinfo.h>
#include <pthread.h>

namespace hello {

EpollServer& EpollServer::getInstance() {
    static EpollServer instance;
    return instance;
}

bool EpollServer::startServer(const std::string& address, uint16_t port) {
    if (running_.load()) {
        std::cout << "EpollServer is already running" << std::endl;
        return false;
    }
    
    server_address_ = address;
    server_port_ = port;
    
    // Initialize NUMA awareness (optional)
#ifdef HAVE_NUMA
    numa_available_ = (numa_available() >= 0);
    if (numa_available_) {
        numa_node_ = numa_node_of_cpu(sched_getcpu());
        std::cout << "NUMA-aware server initialized on node " << numa_node_ << std::endl;
    } else {
        numa_available_ = false;
        std::cout << "NUMA not available, running without NUMA optimizations" << std::endl;
    }
#else
    numa_available_ = false;
    std::cout << "NUMA support not compiled in, running without NUMA optimizations" << std::endl;
#endif
    
    // Get CPU cores for worker threads
    int num_cores = get_nprocs();
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        cpu_cores_.push_back(i % num_cores);
    }
    
    // Create service instances
    service_ = std::make_unique<HelloServiceImpl>();
    
    // Pre-compile common responses for zero-allocation operations
    pre_compiled_hello_response_ = createGrpcResponse("Hello from HFT-optimized server!");
    pre_compiled_error_response_ = createGrpcResponse("Error processing request");
    
    // Optimize memory layout for cache efficiency
    optimizeMemoryLayout();
    
    // Create server socket
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::cerr << "Failed to create server socket" << std::endl;
        return false;
    }
    
    // Set socket options for high performance
    int opt = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR" << std::endl;
        close(server_socket_);
        return false;
    }
    
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set SO_REUSEPORT" << std::endl;
        close(server_socket_);
        return false;
    }
    
    // Set TCP_NODELAY for low latency
    if (setsockopt(server_socket_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set TCP_NODELAY" << std::endl;
    }
    
    // Set socket buffer sizes for high throughput
    int send_buf_size = 1024 * 1024; // 1MB
    int recv_buf_size = 1024 * 1024; // 1MB
    setsockopt(server_socket_, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
    setsockopt(server_socket_, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));
    
    // Set non-blocking mode
    if (!setNonBlocking(server_socket_)) {
        std::cerr << "Failed to set non-blocking mode" << std::endl;
        close(server_socket_);
        return false;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(address.c_str());
    
    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind server socket" << std::endl;
        close(server_socket_);
        return false;
    }
    
    // Listen for connections
    if (listen(server_socket_, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on server socket" << std::endl;
        close(server_socket_);
        return false;
    }
    
    // Initialize epoll
    if (!initializeEpoll()) {
        std::cerr << "Failed to initialize epoll" << std::endl;
        close(server_socket_);
        return false;
    }
    
    // Add server socket to epoll
    if (!addToEpoll(server_socket_, EPOLLIN)) {
        std::cerr << "Failed to add server socket to epoll" << std::endl;
        close(epoll_fd_);
        close(server_socket_);
        return false;
    }
    
    running_.store(true);
    cleanup_running_.store(true);
    
    std::cout << "HFT-optimized EpollServer started on " << address << ":" << port << std::endl;
    std::cout << "Features: Lock-free operations, CPU affinity, NUMA awareness, pre-compiled responses" << std::endl;
    
    // Start worker threads with CPU affinity
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        worker_threads_.emplace_back(&EpollServer::epollWorkerThread, this, i);
    }
    
    // Start cleanup thread
    cleanup_thread_ = std::thread(&EpollServer::cleanupThread, this);
    
    // Pre-warm caches
    preWarmCaches();
    
    return true;
}

void EpollServer::stopServer() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    cleanup_running_.store(false);
    
    // Wake up epoll
    if (epoll_fd_ >= 0) {
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = -1;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, -1, &event);
    }
    
    // Wait for threads to finish
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    // Close connections
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& pair : connections_) {
            close(pair.first);
        }
        connections_.clear();
    }
    
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
    
    std::cout << "HFT-optimized EpollServer stopped" << std::endl;
}

bool EpollServer::isRunning() const {
    return running_.load();
}

// HFT-specific optimizations
bool EpollServer::setCpuAffinity(int cpu_core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core, &cpuset);
    
    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) == 0;
}

bool EpollServer::setNumaAffinity(int numa_node) {
#ifdef HAVE_NUMA
    if (!numa_available_) return false;
    
    unsigned long nodemask = 1UL << numa_node;
    return set_mempolicy(MPOL_BIND, &nodemask, sizeof(nodemask) * 8) == 0;
#else
    (void)numa_node; // Suppress unused parameter warning
    return false;
#endif
}

void EpollServer::optimizeMemoryLayout() {
    // Lock memory to prevent swapping
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: Failed to lock memory pages" << std::endl;
    }
    
    // Set memory policy for NUMA (if available)
#ifdef HAVE_NUMA
    if (numa_available_) {
        setNumaAffinity(numa_node_);
    }
#endif
}

void EpollServer::preWarmCaches() {
    // Pre-warm connection pool
    for (int i = 0; i < 100; ++i) {
        auto* conn = connection_pool_.allocate();
        if (conn) {
            connection_pool_.deallocate(conn);
        }
    }
    
    // Pre-warm response buffers
    std::vector<uint8_t> warmup_data(1024, 0);
    for (int i = 0; i < 1000; ++i) {
        createGrpcResponse("warmup");
    }
}

bool EpollServer::initializeEpoll() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::cerr << "Failed to create epoll instance" << std::endl;
        return false;
    }
    return true;
}

bool EpollServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool EpollServer::addToEpoll(int fd, uint32_t events) {
    struct epoll_event event;
    event.events = events;
    event.data.ptr = nullptr;
    event.data.fd = fd;
    
    return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == 0;
}

bool EpollServer::removeFromEpoll(int fd) {
    return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

void EpollServer::epollWorkerThread(int worker_id) {
    // Set CPU affinity for this worker thread
    if (worker_id < cpu_cores_.size()) {
        setCpuAffinity(cpu_cores_[worker_id]);
        std::cout << "Worker thread " << worker_id << " bound to CPU core " << cpu_cores_[worker_id] << std::endl;
    }
    
    // Set NUMA affinity (if available)
#ifdef HAVE_NUMA
    if (numa_available_) {
        setNumaAffinity(numa_node_);
    }
#endif
    
    struct epoll_event events[MAX_EVENTS];
    
    while (running_.load()) {
        // Use shorter timeout for lower latency
        int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1); // 1ms timeout for HFT
        
        if (num_events < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal
            }
            std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
            break;
        }
        
        stats_.epoll_events_processed.fetch_add(num_events);
        
        // Process events in batches for better cache efficiency
        for (int i = 0; i < num_events; i += BATCH_SIZE) {
            if (!running_.load()) break;
            
            int batch_end = std::min(i + BATCH_SIZE, num_events);
            
            for (int j = i; j < batch_end; ++j) {
                int fd = events[j].data.fd;
                uint32_t event_flags = events[j].events;
                
                if (fd == server_socket_) {
                    // New connection
                    acceptNewConnection();
                } else {
                    // Client connection - use shared_ptr for safety
                    std::shared_ptr<Connection> conn;
                    {
                        std::lock_guard<std::mutex> lock(connections_mutex_);
                        auto it = connections_.find(fd);
                        if (it != connections_.end()) {
                            conn = it->second;
                        }
                    }
                    
                    if (conn) {
                        conn->last_activity = time(nullptr);
                        
                        // Record start time for latency measurement
                        auto start_time = std::chrono::high_resolution_clock::now();
                        
                        if (event_flags & EPOLLIN) {
                            handleClientData(conn.get());
                        }
                        
                        if (event_flags & EPOLLOUT) {
                            handleClientWrite(conn.get());
                        }
                        
                        if (event_flags & (EPOLLERR | EPOLLHUP)) {
                            closeConnection(conn.get());
                        }
                        
                        // Calculate and record latency
                        auto end_time = std::chrono::high_resolution_clock::now();
                        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
                        
                        // Update latency statistics atomically
                        uint64_t current_min = stats_.min_latency_ns.load();
                        while (latency_ns < current_min && 
                               !stats_.min_latency_ns.compare_exchange_weak(current_min, latency_ns)) {}
                        
                        uint64_t current_max = stats_.max_latency_ns.load();
                        while (latency_ns > current_max && 
                               !stats_.max_latency_ns.compare_exchange_weak(current_max, latency_ns)) {}
                        
                        stats_.total_latency_ns.fetch_add(latency_ns);
                        stats_.latency_count.fetch_add(1);
                    }
                }
            }
        }
    }
}

void EpollServer::acceptNewConnection() {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    int client_fd = accept(server_socket_, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_fd < 0) {
        return;
    }
    
    // Check connection limit
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        if (connections_.size() >= MAX_CONNECTIONS) {
            close(client_fd);
            return;
        }
    }
    
    // Set non-blocking mode
    if (!setNonBlocking(client_fd)) {
        close(client_fd);
        return;
    }
    
    // Set TCP_NODELAY for low latency
    int opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    // Set socket buffer sizes for high throughput
    int send_buf_size = 1024 * 1024; // 1MB
    int recv_buf_size = 1024 * 1024; // 1MB
    setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
    setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));
    
    // Try to allocate from memory pool first for zero-allocation
    Connection* pool_conn = connection_pool_.allocate();
    std::shared_ptr<Connection> conn;
    
    if (pool_conn) {
        // Reuse existing connection object
        pool_conn->fd = client_fd;
        pool_conn->read_pos = 0;
        pool_conn->write_pos = 0;
        pool_conn->keep_alive = true;
        pool_conn->last_activity = time(nullptr);
        pool_conn->cpu_core = sched_getcpu();
        // Zero-initialize buffers
        pool_conn->read_buffer.fill(0);
        pool_conn->write_buffer.fill(0);
        for (auto& buf : pool_conn->write_queue) {
            buf.fill(0);
        }
        
        conn = std::shared_ptr<Connection>(pool_conn, [this](Connection* c) {
            connection_pool_.deallocate(c);
        });
        stats_.lock_free_allocations.fetch_add(1);
    } else {
        // Fallback to regular allocation
        conn = std::make_shared<Connection>(client_fd, sched_getcpu());
    }
    
    conn->remote_addr = inet_ntoa(client_addr.sin_addr);
    conn->remote_port = ntohs(client_addr.sin_port);
    
    // Add to epoll with edge-triggered mode for maximum performance
    if (!addToEpoll(client_fd, EPOLLIN | EPOLLET)) {
        close(client_fd);
        return;
    }
    
    // Store connection
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[client_fd] = conn;
    }
    
    stats_.total_connections.fetch_add(1);
    stats_.active_connections.fetch_add(1);
}

void EpollServer::handleClientData(Connection* conn) {
    if (!conn) return; // Safety check
    
    // Use pre-allocated buffer for zero-allocation operations
    ssize_t bytes_read;
    
    // Read all available data (edge-triggered) using pre-allocated buffer
    while ((bytes_read = recv(conn->fd, conn->read_buffer.data() + conn->read_pos, 
                             conn->read_buffer.size() - conn->read_pos, MSG_DONTWAIT)) > 0) {
        conn->read_pos += bytes_read;
        stats_.total_bytes_received.fetch_add(bytes_read);
        
        // Process data immediately for ultra-low latency
        if (conn->read_pos > 0) {
            std::vector<uint8_t> data(conn->read_buffer.begin(), conn->read_buffer.begin() + conn->read_pos);
            processGrpcRequest(conn, data);
            conn->read_pos = 0; // Reset buffer position
        }
    }
    
    if (bytes_read == 0) {
        // Client disconnected
        closeConnection(conn);
        return;
    }
    
    if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Error occurred
        closeConnection(conn);
        return;
    }
}

void EpollServer::handleClientWrite(Connection* conn) {
    if (!conn) return; // Safety check
    
    // Use lock-free write queue for better performance
    std::vector<uint8_t> data;
    
    while (conn->dequeueWrite(data)) {
        ssize_t bytes_sent = send(conn->fd, data.data(), data.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        
        if (bytes_sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, re-queue data
                conn->enqueueWrite(data);
                break;
            } else {
                // Error occurred
                closeConnection(conn);
                return;
            }
        } else if (bytes_sent < static_cast<ssize_t>(data.size())) {
            // Partial send, re-queue remaining data
            std::vector<uint8_t> remaining(data.begin() + bytes_sent, data.end());
            conn->enqueueWrite(remaining);
            stats_.total_bytes_sent.fetch_add(bytes_sent);
            break;
        } else {
            // Complete send
            stats_.total_bytes_sent.fetch_add(bytes_sent);
        }
    }
    
    // Remove write event if queue is empty
    if (!conn->dequeueWrite(data)) {
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = conn->fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn->fd, &event);
    }
}

void EpollServer::closeConnection(Connection* conn) {
    if (!conn) return; // Safety check
    
    removeFromEpoll(conn->fd);
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(conn->fd);
    }
    
    stats_.active_connections.fetch_sub(1);
}

void EpollServer::cleanupInactiveConnections() {
    time_t now = time(nullptr);
    std::vector<int> to_close;
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& pair : connections_) {
            if (now - pair.second->last_activity > CONNECTION_TIMEOUT) {
                to_close.push_back(pair.first);
            }
        }
    }
    
    for (int fd : to_close) {
        Connection* conn = nullptr;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(fd);
            if (it != connections_.end()) {
                conn = it->second.get();
            }
        }
        if (conn) {
            closeConnection(conn);
        }
    }
}

void EpollServer::cleanupThread() {
    while (cleanup_running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(CLEANUP_INTERVAL));
        cleanupInactiveConnections();
    }
}

void EpollServer::processGrpcRequest(Connection* conn, const std::vector<uint8_t>& data) {
    if (!conn || !service_) return; // Safety check
    
    // Simple HTTP/2 frame parsing (simplified for demo)
    if (data.size() < 9) return; // Minimum frame size
    
    // Extract frame header
    uint8_t type = data[3];
    
    if (type == 1) { // HEADERS frame
        try {
            // Use pre-compiled response for common requests (zero-allocation)
            std::vector<uint8_t> response_data;
            
            // Check if this is a simple hello request (most common case)
            if (data.size() > 20 && std::string(data.begin() + 9, data.begin() + 20).find("hello") != std::string::npos) {
                response_data = pre_compiled_hello_response_; // Use pre-compiled response
            } else {
                // Parse gRPC request for custom responses
                std::string response = parseGrpcRequest(data);
                response_data = createGrpcResponse(response);
            }
            
            // Use lock-free queue for better performance
            if (!conn->enqueueWrite(response_data)) {
                // Queue full, fallback to error response
                conn->enqueueWrite(pre_compiled_error_response_);
            }
            
            // Add write event
            struct epoll_event event;
            event.events = EPOLLIN | EPOLLOUT | EPOLLET;
            event.data.fd = conn->fd;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn->fd, &event);
            
            stats_.total_requests.fetch_add(1);
        } catch (const std::exception& e) {
            std::cerr << "Error processing gRPC request: " << e.what() << std::endl;
            // Send error response
            conn->enqueueWrite(pre_compiled_error_response_);
        }
    }
}

std::vector<uint8_t> EpollServer::createGrpcResponse(const std::string& message) {
    // Create HTTP/2 DATA frame
    std::vector<uint8_t> response;
    
    // Frame header (9 bytes)
    uint32_t payload_length = message.length() + 4; // +4 for gRPC status
    response.push_back((payload_length >> 16) & 0xFF);
    response.push_back((payload_length >> 8) & 0xFF);
    response.push_back(payload_length & 0xFF);
    response.push_back(0); // DATA frame type
    response.push_back(0x01); // END_STREAM flag
    response.push_back(0); // Stream ID (1)
    response.push_back(0);
    response.push_back(0);
    response.push_back(1);
    
    // gRPC status (4 bytes)
    response.push_back(0); // No compression
    response.push_back(0);
    response.push_back(0);
    response.push_back(0);
    
    // Message data
    response.insert(response.end(), message.begin(), message.end());
    
    return response;
}

std::string EpollServer::parseGrpcRequest(const std::vector<uint8_t>& data) {
    if (!service_) return "Service not available";
    
    try {
        // Simple gRPC request parsing (simplified)
        if (data.size() < 5) return "Invalid request";
        
        // Skip gRPC header (5 bytes)
        std::string request_data(data.begin() + 5, data.end());
        
        // Create a simple response using the service
        HelloRequest request;
        request.set_name("EpollClient");
        request.set_age(25);
        
        HelloResponse response;
        service_->SayHello(nullptr, &request, &response);
        
        return response.message();
    } catch (const std::exception& e) {
        std::cerr << "Error parsing gRPC request: " << e.what() << std::endl;
        return "Error processing request";
    }
}

} // namespace hello 