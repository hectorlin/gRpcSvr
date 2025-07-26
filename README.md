# 🚀 Optimized gRPC Server

A high-performance gRPC server implementation in C++17 with comprehensive optimization and performance testing capabilities.

## 📁 Project Structure

```
gRpcSvr/
├── protos/                    # Protocol Buffer definitions
│   └── HelloService.proto     # Service definition
├── src/                       # Source code
│   ├── HelloService.h         # Service interface
│   ├── HelloService.cpp       # Service implementation (optimized)
│   ├── ServerManager.h        # Singleton server manager
│   ├── ServerManager.cpp      # Server lifecycle management
│   ├── LoggingInterceptor.h   # Interceptor interface
│   ├── LoggingInterceptor.cpp # Request/response logging
│   ├── main.cpp              # Server entry point
│   ├── test_client.cpp       # Basic client test
│   ├── performance_test.cpp  # Comprehensive performance test
│   ├── simple_performance_test.cpp # Simple performance test
│   └── latency_test.cpp      # Specialized latency test
├── build_direct/             # Build output directory
├── compile_direct.sh         # Direct compilation script
├── PERFORMANCE_REPORT.md     # Performance analysis report
└── README.md                 # This file
```

## ✨ Features

- **High Performance**: Optimized for sub-millisecond latency
- **Singleton Pattern**: Efficient server lifecycle management
- **Logging Interceptor**: Request/response monitoring
- **Thread Safety**: Atomic operations and proper synchronization
- **Performance Testing**: Comprehensive latency and throughput tests
- **Direct Compilation**: No CMake dependency, uses direct g++ compilation

## 🚀 Quick Start

### Prerequisites

```bash
# Install required packages
sudo dnf install gcc-c++ protobuf-compiler grpc-plugins
sudo dnf install protobuf-devel grpc-devel
```

### Compilation

```bash
# Make script executable
chmod +x compile_direct.sh

# Compile the project
./compile_direct.sh
```

### Running the Server

```bash
# Start the optimized server
cd build_direct
./gRpcSvr_optimized
```

### Running Tests

```bash
# Basic client test
./gRpcSvr_client

# Comprehensive performance test
./gRpcSvr_perf_test

# Simple performance test
./gRpcSvr_simple_perf

# Specialized latency test
./gRpcSvr_latency_test
```

## 📊 Performance Results

The optimized server achieves excellent performance:

- **Light Load**: 0.373ms average latency, 2,631 RPS
- **Medium Load**: 0.736ms average latency, 5,347 RPS  
- **High Load**: 1.225ms average latency, 6,418 RPS
- **Success Rate**: 100% across all tests

See `PERFORMANCE_REPORT.md` for detailed analysis.

## 🔧 API

### HelloService

The server implements a `HelloService` with two RPC methods:

#### SayHello (Unary RPC)
```protobuf
rpc SayHello(HelloRequest) returns (HelloResponse);
```

**Request:**
- `name` (string): User's name
- `age` (int32): User's age

**Response:**
- `message` (string): Personalized greeting
- `timestamp` (int64): Response timestamp

#### SayHelloStream (Server Streaming RPC)
```protobuf
rpc SayHelloStream(HelloRequest) returns (stream HelloResponse);
```

Returns a stream of 5 personalized messages with 100ms intervals.

## 🏗️ Architecture

### Server Manager (Singleton)
- Manages server lifecycle (start/stop)
- Thread-safe implementation
- Graceful shutdown handling

### Logging Interceptor
- Logs all incoming requests and outgoing responses
- Captures method names, timestamps, and status codes
- Performance impact minimized

### Optimization Features
- Pre-allocated string buffers
- Object reuse in streaming operations
- Enhanced thread pool configuration
- High-resolution timing precision
- Removed unnecessary I/O operations

## 🧪 Testing

### Performance Tests
- **Latency Test**: Measures request/response times
- **Throughput Test**: Tests requests per second
- **Concurrency Test**: Multi-threaded load testing
- **Streaming Test**: Server streaming performance

### Test Results
All tests show excellent performance with:
- Sub-millisecond latency for typical loads
- High throughput (>6,000 RPS)
- 100% success rate
- Excellent scalability

## 📈 Monitoring

The server includes built-in logging for:
- Request/response details
- Method execution times
- Error conditions
- Server lifecycle events

## 🔒 Signal Handling

The server gracefully handles:
- SIGINT (Ctrl+C)
- SIGTERM
- Proper cleanup and shutdown

## 🚀 Production Deployment

For production use, consider:
- Load balancing across multiple instances
- TLS/SSL encryption
- Rate limiting
- Monitoring and metrics collection
- Connection pooling

## 📝 License

This project is provided as-is for educational and development purposes.

---

**Built with**: C++17, gRPC, Protocol Buffers  
**Performance**: Sub-millisecond latency, 6,000+ RPS  
**Architecture**: Singleton pattern, Interceptor pattern, Thread-safe design 