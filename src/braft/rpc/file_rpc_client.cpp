// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/file_rpc_client.h"

#include <sstream>

namespace braft {

FileRpcClient::FileRpcClient(const std::string& server_addr, int timeout_ms)
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

FileRpcClient::~FileRpcClient() {
    close();
}

bool FileRpcClient::parseAddress(const std::string& addr,
                                 std::string& host,
                                 int& port) {
    size_t colon_pos = addr.rfind(':');
    if (colon_pos == std::string::npos || colon_pos == addr.length() - 1) {
        return false;
    }

    host = addr.substr(0, colon_pos);
    std::string port_str = addr.substr(colon_pos + 1);

    try {
        port = std::stoi(port_str);
        return port > 0 && port < 65536;
    } catch (...) {
        return false;
    }
}

bool FileRpcClient::connect() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_connected) {
        return true;
    }

    try {
        _socket = std::make_shared<apache::thrift::transport::TSocket>(
            _host, _port);
        _socket->setConnTimeout(_timeout_ms);
        _socket->setSendTimeout(_timeout_ms);
        _socket->setRecvTimeout(_timeout_ms);

        _transport = std::make_shared<
            apache::thrift::transport::TBufferedTransport>(_socket);
        _protocol = std::make_shared<
            apache::thrift::protocol::TBinaryProtocol>(_transport);

        _client.reset(new FileServiceClient(_protocol));

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

void FileRpcClient::close() {
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

bool FileRpcClient::isConnected() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _connected && _transport && _transport->isOpen();
}

void FileRpcClient::ensureConnection() {
    if (!isConnected()) {
        if (!connect()) {
            std::ostringstream oss;
            oss << "Failed to connect to " << _server_addr;
            throw std::runtime_error(oss.str());
        }
    }
}

GetFileResponse FileRpcClient::getFile(const GetFileRequest& req) {
    ensureConnection();

    std::lock_guard<std::mutex> lock(_mutex);
    GetFileResponse response;
    _client->getFile(response, req);
    return response;
}

} // namespace braft
