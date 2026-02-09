// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Raft RPC Client - Thrift-based implementation

#ifndef BRAFT_RPC_RAFT_RPC_CLIENT_H
#define BRAFT_RPC_RAFT_RPC_CLIENT_H

#include <string>
#include <memory>
#include <functional>
#include <mutex>

#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

// Generated Thrift headers
#include "RaftService.h"
#include "raft_types.h"

namespace braft {

/**
 * RaftRpcClient - Thrift-based Raft RPC client
 *
 * Provides synchronous RPC calls for Raft protocol:
 * - requestVote
 * - appendEntries
 * - installSnapshot
 * - timeoutNow
 *
 * Thread safety: Each instance should be used by a single thread.
 * For multi-threaded usage, create separate instances or use external synchronization.
 */
class RaftRpcClient {
public:
    /**
     * Constructor
     * @param server_addr Server address in format "host:port"
     * @param timeout_ms Connection timeout in milliseconds (default 5000ms)
     */
    explicit RaftRpcClient(const std::string& server_addr, int timeout_ms = 5000);

    ~RaftRpcClient();

    // Disable copy, allow move
    RaftRpcClient(const RaftRpcClient&) = delete;
    RaftRpcClient& operator=(const RaftRpcClient&) = delete;
    RaftRpcClient(RaftRpcClient&&) = default;
    RaftRpcClient& operator=(RaftRpcClient&&) = default;

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

    // ========== Synchronous RPC Methods ==========

    /**
     * preVote - Pre-vote RPC
     * @param req Request parameters
     * @return Response from server
     * @ Throws std::runtime_error on RPC failure
     */
    RequestVoteResponse preVote(const RequestVoteRequest& req);

    /**
     * requestVote - RequestVote RPC
     * @param req Request parameters
     * @return Response from server
     * @ Throws std::runtime_error on RPC failure
     */
    RequestVoteResponse requestVote(const RequestVoteRequest& req);

    /**
     * appendEntries - AppendEntries RPC (log replication and heartbeat)
     * @param req Request parameters
     * @return Response from server
     * @ Throws std::runtime_error on RPC failure
     */
    AppendEntriesResponse appendEntries(const AppendEntriesRequest& req);

    /**
     * installSnapshot - InstallSnapshot RPC
     * @param req Request parameters
     * @return Response from server
     * @ Throws std::runtime_error on RPC failure
     */
    InstallSnapshotResponse installSnapshot(const InstallSnapshotRequest& req);

    /**
     * timeoutNow - TimeoutNow RPC
     * @param req Request parameters
     * @return Response from server
     * @ Throws std::runtime_error on RPC failure
     */
    TimeoutNowResponse timeoutNow(const TimeoutNowRequest& req);

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
     * @param addr Address string
     * @param host Output host
     * @param port Output port
     * @return true on success
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
    std::unique_ptr<RaftServiceClient> _client;

    mutable std::mutex _mutex;  // Protects _transport, _client
    bool _connected;
};

} // namespace braft

#endif // BRAFT_RPC_RAFT_RPC_CLIENT_H
