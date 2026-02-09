// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// File RPC Client - Thrift-based implementation for file transfer

#ifndef BRAFT_RPC_FILE_RPC_CLIENT_H
#define BRAFT_RPC_FILE_RPC_CLIENT_H

#include <string>
#include <memory>
#include <mutex>

#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

// Generated Thrift headers
#include "FileService.h"
#include "file_service_types.h"

namespace braft {

/**
 * FileRpcClient - Thrift-based file transfer client
 *
 * Used for copying snapshot files from remote nodes.
 * Provides both synchronous and asynchronous file reading.
 */
class FileRpcClient {
public:
    /**
     * Constructor
     * @param server_addr Server address in format "host:port"
     * @param timeout_ms Connection timeout in milliseconds (default 30000ms)
     */
    explicit FileRpcClient(const std::string& server_addr, int timeout_ms = 30000);

    ~FileRpcClient();

    // Disable copy, allow move
    FileRpcClient(const FileRpcClient&) = delete;
    FileRpcClient& operator=(const FileRpcClient&) = delete;
    FileRpcClient(FileRpcClient&&) = default;
    FileRpcClient& operator=(FileRpcClient&&) = default;

    /**
     * Connect to the server
     * @return true on success, false on failure
     */
    bool connect();

    /**
     * Close the connection
     */
    void close();

    /**
     * Check if connected to server
     * @return true if connected
     */
    bool isConnected() const;

    /**
     * getFile - Read a chunk of file data
     * @param req File request with reader_id, filename, count, offset
     * @return Response with eof flag and read_size
     *
     * Note: Actual file data is transported via Thrift attachment.
     * This method only returns metadata.
     */
    GetFileResponse getFile(const GetFileRequest& req);

    /**
     * Get server address
     */
    const std::string& getServerAddr() const { return _server_addr; }

private:
    /**
     * Ensure connection is open
     * Reconnects if connection was closed
     */
    void ensureConnection();

    /**
     * Parse address string "host:port"
     */
    static bool parseAddress(const std::string& addr,
                            std::string& host,
                            int& port);

private:
    std::string _server_addr;
    int _timeout_ms;
    std::string _host;
    int _port;

    // Thrift components
    std::shared_ptr<apache::thrift::transport::TSocket> _socket;
    std::shared_ptr<apache::thrift::transport::TTransport> _transport;
    std::shared_ptr<apache::thrift::protocol::TProtocol> _protocol;
    std::unique_ptr<FileServiceClient> _client;

    mutable std::mutex _mutex;
    bool _connected;
};

} // namespace braft

#endif // BRAFT_RPC_FILE_RPC_CLIENT_H
