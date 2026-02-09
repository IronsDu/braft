// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/thrift_server.h"
#include <iostream>
#include "braft/rpc/raft_rpc_service.h"
#include "braft/rpc/file_rpc_service.h"

#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/concurrency/ThreadFactory.h>

namespace braft {

ThriftServer::ThriftServer()
    : _running(false), _port(0) {
}

ThriftServer::~ThriftServer() {
    stop();
}

bool ThriftServer::start(int port,
                          std::shared_ptr<RaftRpcService> raft_service,
                          std::shared_ptr<FileRpcService> file_service,
                          int num_threads) {
    if (_running.load()) {
        return false;
    }

    _port = port;

    try {
        // Create server socket
        auto server_socket = std::make_shared<
            apache::thrift::transport::TServerSocket>(port);

        // Create transport factory
        auto transport_factory = std::make_shared<
            apache::thrift::transport::TBufferedTransportFactory>();

        // Create protocol factory
        auto protocol_factory = std::make_shared<
            apache::thrift::protocol::TBinaryProtocolFactory>();

        // Create thread factory
        auto thread_factory = std::make_shared<
            apache::thrift::concurrency::ThreadFactory>();

        // Create thread manager
        _thread_manager = apache::thrift::concurrency::ThreadManager::newSimpleThreadManager(
            num_threads);
        _thread_manager->threadFactory(thread_factory);
        _thread_manager->start();

        std::cout << "[ThriftServer] ThreadManager started with " << num_threads
                  << " workers" << std::endl;

        // Create processor
        auto processor = std::make_shared<RaftServiceProcessor>(raft_service);

        // Create server with thread pool
        _server.reset(new apache::thrift::server::TThreadPoolServer(
            processor,
            server_socket,
            transport_factory,
            protocol_factory,
            _thread_manager
        ));

        // Start server in a separate thread
        _running.store(true);
        _server_thread = std::thread([this]() {
            std::cout << "[ThriftServer] Server thread starting serve() on port "
                      << _port << std::endl;
            _server->serve();
            std::cout << "[ThriftServer] Server thread exited serve()" << std::endl;
        });

        // Wait for server to be ready (give it time to start listening)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::cout << "[ThriftServer] Server started on port " << port << std::endl;

        return true;

    } catch (const std::exception& e) {
        _running.store(false);
        _thread_manager.reset();
        _server.reset();
        return false;
    }
}

void ThriftServer::stop() {
    if (!_running.load()) {
        return;
    }

    _running.store(false);

    if (_server) {
        _server->stop();
    }

    if (_thread_manager) {
        _thread_manager->stop();
    }

    if (_server_thread.joinable()) {
        _server_thread.join();
    }

    _thread_manager.reset();
    _server.reset();
}

void ThriftServer::join() {
    if (_server_thread.joinable()) {
        _server_thread.join();
    }
}

} // namespace braft
