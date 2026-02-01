// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/raft_rpc_client.h"

#include <sstream>
#include <stdexcept>

namespace braft {

RaftRpcClient::RaftRpcClient(const std::string& server_addr, int timeout_ms)
    : _server_addr(server_addr),
      _timeout_ms(timeout_ms),
      _port(0),
      _connected(false) {

    if (!parseAddress(server_addr, _host, _port)) {
        std::ostringstream oss;
        oss << "Invalid server address: " << server_addr;
        throw std::invalid_argument(oss.str());
    }
}

RaftRpcClient::~RaftRpcClient() {
    close();
}

bool RaftRpcClient::parseAddress(const std::string& addr,
                                 std::string& host,
                                 int& port) {
    // Handle format: "host:port" or "host:port:index" (braft server_id format)
    // We need to extract host:port, ignoring the index if present

    size_t last_colon = addr.rfind(':');
    if (last_colon == std::string::npos || last_colon == addr.length() - 1) {
        return false;
    }

    // Check if there's an index after the port (format: host:port:index)
    size_t second_last_colon = addr.rfind(':', last_colon - 1);

    std::string port_str;
    if (second_last_colon != std::string::npos) {
        // Format: host:port:index
        host = addr.substr(0, second_last_colon);
        port_str = addr.substr(second_last_colon + 1, last_colon - second_last_colon - 1);
    } else {
        // Format: host:port
        host = addr.substr(0, last_colon);
        port_str = addr.substr(last_colon + 1);
    }

    try {
        port = std::stoi(port_str);
        return port > 0 && port < 65536;
    } catch (...) {
        return false;
    }
}

bool RaftRpcClient::connect() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_connected) {
        return true;
    }

    try {
        // Create Thrift socket
        _socket = std::make_shared<apache::thrift::transport::TSocket>(
            _host, _port);
        _socket->setConnTimeout(_timeout_ms);
        _socket->setSendTimeout(_timeout_ms);
        _socket->setRecvTimeout(_timeout_ms);

        // Create transport and protocol
        _transport = std::make_shared<
            apache::thrift::transport::TBufferedTransport>(_socket);
        _protocol = std::make_shared<
            apache::thrift::protocol::TBinaryProtocol>(_transport);

        // Create client
        _client.reset(new RaftServiceClient(_protocol));

        // Open transport
        _transport->open();
        _connected = true;
        return true;

    } catch (const std::exception& e) {
        _connected = false;
        _client.reset();
        _transport.reset();
        _socket.reset();
        return false;
    }
}

void RaftRpcClient::close() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_transport && _transport->isOpen()) {
        try {
            _transport->close();
        } catch (...) {
            // Ignore errors during close
        }
    }

    _connected = false;
    _client.reset();
    _transport.reset();
    _socket.reset();
}

bool RaftRpcClient::isConnected() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _connected && _transport && _transport->isOpen();
}

void RaftRpcClient::ensureConnection() {
    if (!isConnected()) {
        if (!connect()) {
            std::ostringstream oss;
            oss << "Failed to connect to " << _server_addr;
            throw std::runtime_error(oss.str());
        }
    }
}

RequestVoteResponse RaftRpcClient::preVote(const RequestVoteRequest& req) {
    ensureConnection();

    std::lock_guard<std::mutex> lock(_mutex);
    RequestVoteResponse response;
    _client->preVote(response, req);
    return response;
}

RequestVoteResponse RaftRpcClient::requestVote(const RequestVoteRequest& req) {
    ensureConnection();

    std::lock_guard<std::mutex> lock(_mutex);
    RequestVoteResponse response;
    _client->requestVote(response, req);
    return response;
}

AppendEntriesResponse RaftRpcClient::appendEntries(
    const AppendEntriesRequest& req) {
    ensureConnection();

    std::lock_guard<std::mutex> lock(_mutex);
    AppendEntriesResponse response;
    _client->appendEntries(response, req);
    return response;
}

InstallSnapshotResponse RaftRpcClient::installSnapshot(
    const InstallSnapshotRequest& req) {
    ensureConnection();

    std::lock_guard<std::mutex> lock(_mutex);
    InstallSnapshotResponse response;
    _client->installSnapshot(response, req);
    return response;
}

TimeoutNowResponse RaftRpcClient::timeoutNow(const TimeoutNowRequest& req) {
    ensureConnection();

    std::lock_guard<std::mutex> lock(_mutex);
    TimeoutNowResponse response;
    _client->timeoutNow(response, req);
    return response;
}

} // namespace braft
