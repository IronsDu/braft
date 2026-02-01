// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/raft_rpc_service.h"
#include "braft/v2/node.h"

namespace braft {

// Constructor for v2::Node
RaftRpcService::RaftRpcService(v2::Node* node)
    : _node_v2(node),
      _node_legacy(nullptr),
      _use_v2(true) {
}

// Constructor for legacy Node
RaftRpcService::RaftRpcService(Node* node)
    : _node_v2(nullptr),
      _node_legacy(node),
      _use_v2(false) {
}

RaftRpcService::~RaftRpcService() {
}

void RaftRpcService::preVote(RequestVoteResponse& _return,
                              const RequestVoteRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handlePreVote(req, _return);
    } else if (_node_legacy) {
        // TODO: Call legacy Node's pre-vote handler
        _return.term = req.term;
        _return.granted = false;
    }
}

void RaftRpcService::requestVote(RequestVoteResponse& _return,
                                  const RequestVoteRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleRequestVote(req, _return);
    } else if (_node_legacy) {
        // TODO: Call legacy Node's request vote handler
        _return.term = req.term;
        _return.granted = false;
    }
}

void RaftRpcService::appendEntries(AppendEntriesResponse& _return,
                                     const AppendEntriesRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleAppendEntries(req, _return);
    } else if (_node_legacy) {
        // TODO: Call legacy Node's append entries handler
        _return.term = req.term;
        _return.success = false;
    }
}

void RaftRpcService::installSnapshot(InstallSnapshotResponse& _return,
                                      const InstallSnapshotRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleInstallSnapshot(req, _return);
    } else if (_node_legacy) {
        // TODO: Call legacy Node's install snapshot handler
        _return.term = req.term;
        _return.success = false;
    }
}

void RaftRpcService::timeoutNow(TimeoutNowResponse& _return,
                                 const TimeoutNowRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleTimeoutNow(req, _return);
    } else if (_node_legacy) {
        // TODO: Call legacy Node's timeout now handler
        _return.term = req.term;
        _return.success = false;
    }
}

} // namespace braft
