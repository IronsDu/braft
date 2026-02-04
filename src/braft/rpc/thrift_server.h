// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Thrift Server - Wrapper for Apache Thrift server

#ifndef BRAFT_RPC_THRIFT_SERVER_H
#define BRAFT_RPC_THRIFT_SERVER_H

#include <memory>
#include <thread>
#include <atomic>

#include <thrift/server/TThreadPoolServer.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/transport/TServerSocket.h>

namespace braft {

// Forward declarations
class RaftRpcService;
class FileRpcService;

/**
 * ThriftServer - Wrapper for Apache Thrift server
 *
 * Manages a multi-threaded Thrift server that handles Raft and File RPCs.
 */
class ThriftServer {
public:
    ThriftServer();
    ~ThriftServer();

    // Disable copy
    ThriftServer(const ThriftServer&) = delete;
    ThriftServer& operator=(const ThriftServer&) = delete;

    /**
     * Start the server
     * @param port Port to listen on
     * @param raft_service Raft RPC service handler
     * @param file_service File RPC service handler (optional)
     * @param num_threads Number of worker threads (default 4)
     * @return true on success, false on failure
     */
    bool start(int port,
               std::shared_ptr<RaftRpcService> raft_service,
               std::shared_ptr<FileRpcService> file_service = nullptr,
               int num_threads = 4);

    /**
     * Stop the server
     * Blocks until server is stopped
     */
    void stop();

    /**
     * Wait for server to finish
     */
    void join();

    /**
     * Check if server is running
     */
    bool isRunning() const { return _running.load(); }

    /**
     * Get the port the server is listening on
     */
    int getPort() const { return _port; }

private:
    std::unique_ptr<apache::thrift::server::TThreadPoolServer> _server;
    std::shared_ptr<apache::thrift::concurrency::ThreadManager> _thread_manager;
    std::thread _server_thread;
    std::atomic<bool> _running;
    int _port;
};

} // namespace braft

#endif // BRAFT_RPC_THRIFT_SERVER_H
