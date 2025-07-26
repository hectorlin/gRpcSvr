#include "EpollServer.h"
#include "HelloService.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <chrono>
#include <algorithm>

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
    
    // Create service instances
    service_ = std::make_unique<HelloServiceImpl>();
    
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
    
    std::cout << "EpollServer started on " << address << ":" << port << std::endl;
    std::cout << "Optimized with epoll for maximum I/O performance" << std::endl;
    
    // Start worker threads
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        worker_threads_.emplace_back(&EpollServer::epollWorkerThread, this);
    }
    
    // Start cleanup thread
    cleanup_thread_ = std::thread(&EpollServer::cleanupThread, this);
    
    return true;
}

void EpollServer::stopServer() {
    if (!running_.load()) {
        std::cout << "EpollServer is not running" << std::endl;
        return;
    }
    
    std::cout << "Stopping EpollServer..." << std::endl;
    
    running_.store(false);
    cleanup_running_.store(false);
    
    // Close server socket to wake up epoll
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
    
    // Wait for worker threads
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    
    // Wait for cleanup thread
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    // Close epoll
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    
    // Close all connections
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.clear();
    }
    
    std::cout << "EpollServer stopped" << std::endl;
}

bool EpollServer::isRunning() const {
    return running_.load();
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

void EpollServer::epollWorkerThread() {
    struct epoll_event events[MAX_EVENTS];
    
    while (running_.load()) {
        int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100); // 100ms timeout
        
        if (num_events < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal
            }
            std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
            break;
        }
        
        stats_.epoll_events_processed.fetch_add(num_events);
        
        for (int i = 0; i < num_events; ++i) {
            if (!running_.load()) break;
            
            int fd = events[i].data.fd;
            uint32_t event_flags = events[i].events;
            
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
                    
                    if (event_flags & EPOLLIN) {
                        handleClientData(conn.get());
                    }
                    
                    if (event_flags & EPOLLOUT) {
                        handleClientWrite(conn.get());
                    }
                    
                    if (event_flags & (EPOLLERR | EPOLLHUP)) {
                        closeConnection(conn.get());
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
    
    // Create connection object
    auto conn = std::make_shared<Connection>(client_fd);
    conn->remote_addr = inet_ntoa(client_addr.sin_addr);
    conn->remote_port = ntohs(client_addr.sin_port);
    
    // Add to epoll
    if (!addToEpoll(client_fd, EPOLLIN | EPOLLET)) { // Edge-triggered
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
    
    char buffer[4096];
    ssize_t bytes_read;
    
    // Read all available data (edge-triggered)
    while ((bytes_read = recv(conn->fd, buffer, sizeof(buffer), 0)) > 0) {
        conn->read_buffer.insert(conn->read_buffer.end(), buffer, buffer + bytes_read);
        stats_.total_bytes_received.fetch_add(bytes_read);
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
    
    // Process complete requests
    if (!conn->read_buffer.empty()) {
        processGrpcRequest(conn, conn->read_buffer);
        conn->read_buffer.clear();
    }
}

void EpollServer::handleClientWrite(Connection* conn) {
    if (!conn) return; // Safety check
    
    std::lock_guard<std::mutex> lock(conn->write_mutex);
    
    while (!conn->write_queue.empty()) {
        auto& data = conn->write_queue.front();
        ssize_t bytes_sent = send(conn->fd, data.data(), data.size(), MSG_NOSIGNAL);
        
        if (bytes_sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, keep data in queue
                break;
            } else {
                // Error occurred
                closeConnection(conn);
                return;
            }
        } else if (bytes_sent < static_cast<ssize_t>(data.size())) {
            // Partial send, update data
            data.erase(data.begin(), data.begin() + bytes_sent);
            stats_.total_bytes_sent.fetch_add(bytes_sent);
            break;
        } else {
            // Complete send
            stats_.total_bytes_sent.fetch_add(bytes_sent);
            conn->write_queue.pop();
        }
    }
    
    // Remove write event if queue is empty
    if (conn->write_queue.empty()) {
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
            // Parse gRPC request
            std::string response = parseGrpcRequest(data);
            
            // Create gRPC response
            auto response_data = createGrpcResponse(response);
            
            // Queue response
            {
                std::lock_guard<std::mutex> lock(conn->write_mutex);
                conn->write_queue.push(std::move(response_data));
            }
            
            // Add write event
            struct epoll_event event;
            event.events = EPOLLIN | EPOLLOUT | EPOLLET;
            event.data.fd = conn->fd;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn->fd, &event);
            
            stats_.total_requests.fetch_add(1);
        } catch (const std::exception& e) {
            std::cerr << "Error processing gRPC request: " << e.what() << std::endl;
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