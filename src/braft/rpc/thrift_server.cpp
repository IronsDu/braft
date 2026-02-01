// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/thrift_server.h"
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

        // Create processor
        auto processor = std::make_shared<RaftServiceProcessor>(raft_service);

        // Create server (simple version without ThreadManager)
        _server.reset(new apache::thrift::server::TThreadedServer(
            processor,
            server_socket,
            transport_factory,
            protocol_factory,
            thread_factory
        ));

        // Start server in a separate thread
        _running.store(true);
        _server_thread = std::thread([this]() {
            _server->serve();
        });

        return true;

    } catch (const std::exception& e) {
        _running.store(false);
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

    if (_server_thread.joinable()) {
        _server_thread.join();
    }

    _server.reset();
}

void ThriftServer::join() {
    if (_server_thread.joinable()) {
        _server_thread.join();
    }
}

} // namespace braft
