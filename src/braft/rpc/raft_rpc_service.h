// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Raft RPC Service - Thrift-based implementation

#ifndef BRAFT_RPC_RAFT_RPC_SERVICE_H
#define BRAFT_RPC_RAFT_RPC_SERVICE_H

#include "RaftService.h"

namespace braft {

// Forward declarations
class Node;
namespace v2 {
class Node;
}

/**
 * RaftRpcService - Implementation of RaftServiceIf
 *
 * Receives RPC calls from other Raft nodes and delegates to Node.
 * Supports both legacy Node and v2::Node.
 *
 * Thread safety: This class is called by Thrift server threads.
 */
class RaftRpcService : public RaftServiceIf {
public:
    /**
     * Constructor for v2::Node
     * @param node Pointer to v2::Node instance (must outlive this service)
     */
    explicit RaftRpcService(v2::Node* node);

    /**
     * Constructor for legacy Node
     * @param node Pointer to Node instance (must outlive this service)
     */
    explicit RaftRpcService(Node* node);

    ~RaftRpcService() override;

    // ========== RaftServiceIf Implementation ==========

    void preVote(RequestVoteResponse& _return,
                 const RequestVoteRequest& req) override;

    void requestVote(RequestVoteResponse& _return,
                     const RequestVoteRequest& req) override;

    void appendEntries(AppendEntriesResponse& _return,
                       const AppendEntriesRequest& req) override;

    void installSnapshot(InstallSnapshotResponse& _return,
                         const InstallSnapshotRequest& req) override;

    void timeoutNow(TimeoutNowResponse& _return,
                    const TimeoutNowRequest& req) override;

private:
    v2::Node* _node_v2;  // v2::Node (preferred)
    Node* _node_legacy;   // Legacy Node (for compatibility)
    bool _use_v2;         // Which node to use
};

} // namespace braft

#endif // BRAFT_RPC_RAFT_RPC_SERVICE_H
