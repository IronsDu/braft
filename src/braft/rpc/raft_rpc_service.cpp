// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/raft_rpc_service.h"
#include "braft/v2/node.h"

// Note: Legacy Node support is removed to avoid Protobuf/Thrift type conflicts
// The v2 module uses pure Thrift types and should not depend on Protobuf

namespace braft {

// Constructor for v2::Node
RaftRpcService::RaftRpcService(v2::Node* node)
    : _node_v2(node),
      _node_legacy(nullptr),
      _use_v2(true) {
}

RaftRpcService::~RaftRpcService() {
}

void RaftRpcService::preVote(RequestVoteResponse& _return,
                              const RequestVoteRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handlePreVote(req, _return);
    }
}

void RaftRpcService::requestVote(RequestVoteResponse& _return,
                                  const RequestVoteRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleRequestVote(req, _return);
    }
}

void RaftRpcService::appendEntries(AppendEntriesResponse& _return,
                                     const AppendEntriesRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleAppendEntries(req, _return);
    }
}

void RaftRpcService::installSnapshot(InstallSnapshotResponse& _return,
                                      const InstallSnapshotRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleInstallSnapshot(req, _return);
    }
}

void RaftRpcService::timeoutNow(TimeoutNowResponse& _return,
                                 const TimeoutNowRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleTimeoutNow(req, _return);
    }
}

} // namespace braft
