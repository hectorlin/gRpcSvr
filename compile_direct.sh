#!/bin/bash

# Direct gRPC Server Compilation Script (No CMake)
# Author: AI Assistant
# Date: $(date)

set -e  # Exit on any error

echo "=========================================="
echo "Direct gRPC Server Compilation Script"
echo "=========================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "protos/HelloService.proto" ]; then
    print_error "protos/HelloService.proto not found. Please run this script from the project root directory."
    exit 1
fi

# Check dependencies
print_status "Checking dependencies..."

# Check for required tools
if ! command -v g++ &> /dev/null; then
    print_error "g++ not found. Please install gcc-c++."
    exit 1
fi

if ! command -v protoc &> /dev/null; then
    print_error "protoc not found. Please install protobuf-compiler."
    exit 1
fi

if ! command -v grpc_cpp_plugin &> /dev/null; then
    print_error "grpc_cpp_plugin not found. Please install grpc-plugins."
    exit 1
fi

# Check for required libraries
if ! pkg-config --exists protobuf; then
    print_error "protobuf development libraries not found."
    exit 1
fi

if ! pkg-config --exists grpc++; then
    print_error "grpc++ development libraries not found."
    exit 1
fi

print_success "All dependencies found!"

# Create build directory
print_status "Creating build directory..."
if [ -d "build_direct" ]; then
    print_warning "Build directory already exists. Cleaning..."
    rm -rf build_direct
fi
mkdir -p build_direct
cd build_direct

# Generate protobuf and gRPC files
print_status "Generating protobuf and gRPC files..."

# Generate protobuf files
protoc --cpp_out=. -I../protos ../protos/HelloService.proto
if [ $? -eq 0 ]; then
    print_success "Protobuf files generated"
else
    print_error "Failed to generate protobuf files"
    exit 1
fi

# Generate gRPC files
protoc --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` -I../protos ../protos/HelloService.proto
if [ $? -eq 0 ]; then
    print_success "gRPC files generated"
else
    print_error "Failed to generate gRPC files"
    exit 1
fi

# Get compiler flags from pkg-config
print_status "Getting compiler flags..."
PROTOBUF_FLAGS=$(pkg-config --cflags --libs protobuf)
GRPC_FLAGS=$(pkg-config --cflags --libs grpc++)
GRPC_PLUS_PLUS_FLAGS=$(pkg-config --cflags --libs grpc++)

# Common compiler flags
CXX_FLAGS="-std=c++17 -Wall -Wextra -O2 -pthread"
INCLUDE_FLAGS="-I. -I../src"

print_status "Compiling optimized server executable..."

# Compile optimized server
g++ $CXX_FLAGS $INCLUDE_FLAGS \
    ../src/main.cpp \
    ../src/HelloService.cpp \
    ../src/ServerManager.cpp \
    ../src/LoggingInterceptor.cpp \
    HelloService.pb.cc \
    HelloService.grpc.pb.cc \
    $PROTOBUF_FLAGS $GRPC_PLUS_PLUS_FLAGS \
    -o gRpcSvr_optimized

if [ $? -eq 0 ]; then
    print_success "Optimized server compiled successfully"
else
    print_error "Optimized server compilation failed"
    exit 1
fi

print_status "Compiling epoll-optimized server executable..."

# Compile epoll server
g++ $CXX_FLAGS $INCLUDE_FLAGS \
    ../src/main_epoll.cpp \
    ../src/EpollServer.cpp \
    ../src/HelloService.cpp \
    ../src/LoggingInterceptor.cpp \
    HelloService.pb.cc \
    HelloService.grpc.pb.cc \
    $PROTOBUF_FLAGS $GRPC_PLUS_PLUS_FLAGS \
    -o gRpcSvr_epoll

if [ $? -eq 0 ]; then
    print_success "Epoll server compiled successfully"
else
    print_error "Epoll server compilation failed"
    exit 1
fi

print_status "Compiling client executable..."

# Compile client
g++ $CXX_FLAGS $INCLUDE_FLAGS \
    ../src/test_client.cpp \
    HelloService.pb.cc \
    HelloService.grpc.pb.cc \
    $PROTOBUF_FLAGS $GRPC_PLUS_PLUS_FLAGS \
    -o gRpcSvr_client

if [ $? -eq 0 ]; then
    print_success "Client compiled successfully"
else
    print_error "Client compilation failed"
    exit 1
fi

print_status "Compiling performance test executable..."

# Compile performance test
g++ $CXX_FLAGS $INCLUDE_FLAGS \
    ../src/performance_test.cpp \
    HelloService.pb.cc \
    HelloService.grpc.pb.cc \
    $PROTOBUF_FLAGS $GRPC_PLUS_PLUS_FLAGS \
    -o gRpcSvr_perf_test

if [ $? -eq 0 ]; then
    print_success "Performance test compiled successfully"
else
    print_error "Performance test compilation failed"
    exit 1
fi

print_status "Compiling simple performance test executable..."

# Compile simple performance test
g++ $CXX_FLAGS $INCLUDE_FLAGS \
    ../src/simple_performance_test.cpp \
    HelloService.pb.cc \
    HelloService.grpc.pb.cc \
    $PROTOBUF_FLAGS $GRPC_PLUS_PLUS_FLAGS \
    -o gRpcSvr_simple_perf

if [ $? -eq 0 ]; then
    print_success "Simple performance test compiled successfully"
else
    print_error "Simple performance test compilation failed"
    exit 1
fi

print_status "Compiling latency test executable..."

# Compile latency test
g++ $CXX_FLAGS $INCLUDE_FLAGS \
    ../src/latency_test.cpp \
    HelloService.pb.cc \
    HelloService.grpc.pb.cc \
    $PROTOBUF_FLAGS $GRPC_PLUS_PLUS_FLAGS \
    -o gRpcSvr_latency_test

if [ $? -eq 0 ]; then
    print_success "Latency test compiled successfully"
else
    print_error "Latency test compilation failed"
    exit 1
fi

print_status "Compiling epoll performance test executable..."

# Compile epoll performance test
g++ $CXX_FLAGS $INCLUDE_FLAGS \
    ../src/epoll_performance_test.cpp \
    -o gRpcSvr_epoll_perf_test

if [ $? -eq 0 ]; then
    print_success "Epoll performance test compiled successfully"
else
    print_error "Epoll performance test compilation failed"
    exit 1
fi

# List generated executables
echo ""
print_status "Generated executables:"
if [ -f "gRpcSvr_optimized" ]; then
    echo -e "  ${GREEN}✓${NC} gRpcSvr_optimized (Optimized Server)"
    ls -lh gRpcSvr_optimized
fi

if [ -f "gRpcSvr_epoll" ]; then
    echo -e "  ${GREEN}✓${NC} gRpcSvr_epoll (Epoll-Optimized Server)"
    ls -lh gRpcSvr_epoll
fi

if [ -f "gRpcSvr_client" ]; then
    echo -e "  ${GREEN}✓${NC} gRpcSvr_client (Client)"
    ls -lh gRpcSvr_client
fi

if [ -f "gRpcSvr_perf_test" ]; then
    echo -e "  ${GREEN}✓${NC} gRpcSvr_perf_test (Performance Test)"
    ls -lh gRpcSvr_perf_test
fi

if [ -f "gRpcSvr_simple_perf" ]; then
    echo -e "  ${GREEN}✓${NC} gRpcSvr_simple_perf (Simple Performance Test)"
    ls -lh gRpcSvr_simple_perf
fi

if [ -f "gRpcSvr_latency_test" ]; then
    echo -e "  ${GREEN}✓${NC} gRpcSvr_latency_test (Latency Test)"
    ls -lh gRpcSvr_latency_test
fi

if [ -f "gRpcSvr_epoll_perf_test" ]; then
    echo -e "  ${GREEN}✓${NC} gRpcSvr_epoll_perf_test (Epoll Performance Test)"
    ls -lh gRpcSvr_epoll_perf_test
fi

echo ""
print_success "Direct compilation completed successfully!"
echo "All executables are in the build_direct directory."
echo ""
print_status "To run the optimized server:"
echo "  cd build_direct && ./gRpcSvr_optimized"
echo ""
print_status "To run the epoll-optimized server:"
echo "  cd build_direct && ./gRpcSvr_epoll"
echo ""
print_status "To run epoll performance tests:"
echo "  cd build_direct && ./gRpcSvr_epoll_perf_test"
echo ""
print_status "To run other tests:"
echo "  cd build_direct && ./gRpcSvr_client"
echo "  cd build_direct && ./gRpcSvr_perf_test"
echo "  cd build_direct && ./gRpcSvr_simple_perf"
echo "  cd build_direct && ./gRpcSvr_latency_test"

echo ""
echo "=========================================="
echo "Direct Compilation Summary:"
echo "=========================================="
echo "✓ C++17 gRPC Server with Service Implementation"
echo "✓ Singleton Pattern Server Manager"
echo "✓ Logging Interceptor"
echo "✓ Thread-safe Design"
echo "✓ Performance Testing Framework"
echo "✓ Direct g++ compilation (No CMake)"
echo "✓ Optimized for high performance"
echo "✓ Specialized latency testing"
echo "✓ Epoll-optimized server with I/O multiplexing"
echo "✓ Edge-triggered event handling"
echo "✓ Non-blocking socket operations"
echo "==========================================" 