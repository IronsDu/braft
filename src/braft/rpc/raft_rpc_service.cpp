// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/raft_rpc_service.h"
#include "braft/v2/node.h"
#include "braft/node.h"  // legacy Node
#include "braft/raft.pb.h"  // protobuf messages
#include "braft/compat/brpc_adapter.h"  // brpc 适配器
#include "braft/closure_queue.h"  // ClosureQueue
#include <memory>

namespace braft {

// 简单的 Closure 实现，用于同步调用
class SyncClosure : public google::protobuf::Closure {
public:
    SyncClosure() : _done(false), _rc(0) {}

    void Run() override {
        _done = true;
        _rc = _rc_temp;
    }

    void setResult(int rc) { _rc_temp = rc; }

    void wait() {
        while (!_done) {
            usleep(1000);  // 1ms
        }
    }

    int getRC() const { return _rc; }

private:
    bool _done;
    int _rc;
    int _rc_temp;
};

} // namespace braft {

// Helper functions to convert Thrift messages to Protobuf messages

// Convert Thrift RequestVoteRequest to Protobuf RequestVoteRequest
::RequestVoteRequest to_protobuf(const RequestVoteRequest& thrift_req) {
    ::RequestVoteRequest proto_req;
    proto_req.set_group_id(thrift_req.group_id);
    proto_req.set_server_id(thrift_req.server_id);
    proto_req.set_peer_id(thrift_req.peer_id);
    proto_req.set_term(thrift_req.term);
    proto_req.set_last_log_term(thrift_req.last_log_term);
    proto_req.set_last_log_index(thrift_req.last_log_index);

    // Handle disrupted_leader field if present
    if (thrift_req.__isset.disrupted_leader) {
        auto* leader = proto_req.mutable_disrupted_leader();
        leader->set_term(thrift_req.disrupted_leader.term);
        leader->set_peer_id(thrift_req.disrupted_leader.peer_id);
    }

    return proto_req;
}

// Convert Protobuf RequestVoteResponse to Thrift RequestVoteResponse
void from_protobuf(const ::RequestVoteResponse& proto_resp, RequestVoteResponse& thrift_resp) {
    thrift_resp.term = proto_resp.term();
    thrift_resp.granted = proto_resp.granted();

    if (proto_resp.has_disrupted()) {
        thrift_resp.__set_disrupted(true);
        thrift_resp.__set_disrupted_leader_term(proto_resp.disrupted().term());
        thrift_resp.__set_disrupted_leader_peer_id(proto_resp.disrupted().peer_id());
    }

    if (proto_resp.has_rejected_by_lease()) {
        thrift_resp.__set_rejected_by_lease(true);
    }
}

// Convert Thrift AppendEntriesRequest to Protobuf AppendEntriesRequest
::AppendEntriesRequest to_protobuf(const AppendEntriesRequest& thrift_req) {
    ::AppendEntriesRequest proto_req;
    proto_req.set_group_id(thrift_req.group_id);
    proto_req.set_server_id(thrift_req.server_id);
    proto_req.set_peer_id(thrift_req.peer_id);
    proto_req.set_term(thrift_req.term);
    proto_req.set_prev_log_term(thrift_req.prev_log_term);
    proto_req.set_prev_log_index(thrift_req.prev_log_index);
    proto_req.set_committed_index(thrift_req.committed_index);

    // Convert entries
    for (const auto& thrift_entry : thrift_req.entries) {
        auto* proto_entry = proto_req.add_entries();
        proto_entry->set_term(thrift_entry.term);
        proto_entry->set_index(thrift_entry.index);
        proto_entry->set_type(thrift_entry.type);

        // Note: data field is not set here as Thrift doesn't carry entry data
    }

    return proto_req;
}

// Convert Protobuf AppendEntriesResponse to Thrift AppendEntriesResponse
void from_protobuf(const ::AppendEntriesResponse& proto_resp, AppendEntriesResponse& thrift_resp) {
    thrift_resp.term = proto_resp.term();
    thrift_resp.success = proto_resp.success();
    thrift_resp.__set_last_log_index(proto_resp.last_log_index());
}

// Convert Thrift TimeoutNowRequest to Protobuf TimeoutNowRequest
::TimeoutNowRequest to_protobuf(const TimeoutNowRequest& thrift_req) {
    ::TimeoutNowRequest proto_req;
    proto_req.set_group_id(thrift_req.group_id);
    proto_req.set_server_id(thrift_req.server_id);
    proto_req.set_peer_id(thrift_req.peer_id);
    proto_req.set_term(thrift_req.term);

    return proto_req;
}

// Convert Protobuf TimeoutNowResponse to Thrift TimeoutNowResponse
void from_protobuf(const ::TimeoutNowResponse& proto_resp, TimeoutNowResponse& thrift_resp) {
    thrift_resp.term = proto_resp.term();
    thrift_resp.success = proto_resp.success();
}

// Convert Thrift InstallSnapshotRequest to Protobuf InstallSnapshotRequest
::InstallSnapshotRequest to_protobuf(const InstallSnapshotRequest& thrift_req) {
    ::InstallSnapshotRequest proto_req;
    proto_req.set_group_id(thrift_req.group_id);
    proto_req.set_server_id(thrift_req.server_id);
    proto_req.set_peer_id(thrift_req.peer_id);
    proto_req.set_term(thrift_req.term);
    proto_req.set_last_included_index(thrift_req.last_included_index);
    proto_req.set_last_included_term(thrift_req.last_included_term);

    // Set snapshot meta data
    if (thrift_req.__isset.meta) {
        auto* meta = proto_req.mutable_meta();
        meta->set_last_included_index(thrift_req.meta.last_included_index);
        meta->set_last_included_term(thrift_req.meta.last_included_term);
        // Copy peers
        for (const auto& peer : thrift_req.meta.peers) {
            meta->add_peers(peer);
        }
        for (const auto& peer : thrift_req.meta.old_peers) {
            meta->add_old_peers(peer);
        }
    }

    return proto_req;
}

// Convert Protobuf InstallSnapshotResponse to Thrift InstallSnapshotResponse
void from_protobuf(const ::InstallSnapshotResponse& proto_resp, InstallSnapshotResponse& thrift_resp) {
    thrift_resp.term = proto_resp.term();
    thrift_resp.success = proto_resp.success();
}

} // namespace braft {

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
        // Convert Thrift request to Protobuf
        ::RequestVoteRequest proto_req = to_protobuf(req);
        ::RequestVoteResponse proto_resp;

        // Call legacy Node's handle_pre_vote_request
        int rc = _node_legacy->handle_pre_vote_request(&proto_req, &proto_resp);

        // Convert Protobuf response back to Thrift
        from_protobuf(proto_resp, _return);

        // Handle error
        if (rc != 0) {
            _return.__set_success(false);
        }
    }
}

void RaftRpcService::requestVote(RequestVoteResponse& _return,
                                  const RequestVoteRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleRequestVote(req, _return);
    } else if (_node_legacy) {
        // Convert Thrift request to Protobuf
        ::RequestVoteRequest proto_req = to_protobuf(req);
        ::RequestVoteResponse proto_resp;

        // Call legacy Node's handle_request_vote_request
        int rc = _node_legacy->handle_request_vote_request(&proto_req, &proto_resp);

        // Convert Protobuf response back to Thrift
        from_protobuf(proto_resp, _return);

        // Handle error
        if (rc != 0) {
            _return.__set_success(false);
        }
    }
}

void RaftRpcService::appendEntries(AppendEntriesResponse& _return,
                                     const AppendEntriesRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleAppendEntries(req, _return);
    } else if (_node_legacy) {
        // Convert Thrift request to Protobuf
        ::AppendEntriesRequest proto_req = to_protobuf(req);
        ::AppendEntriesResponse proto_resp;

        // Create Controller and Closure
        Controller cntl;
        SyncClosure done;
        brpc::ClosureGuard done_guard(&done);

        // Call legacy Node's handle_append_entries_request
        _node_legacy->handle_append_entries_request(&cntl, &proto_req, &proto_resp, &done);

        // Convert Protobuf response back to Thrift
        from_protobuf(proto_resp, _return);

        // Check for errors
        if (cntl.Failed()) {
            _return.__set_success(false);
        }
    }
}

void RaftRpcService::installSnapshot(InstallSnapshotResponse& _return,
                                      const InstallSnapshotRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleInstallSnapshot(req, _return);
    } else if (_node_legacy) {
        // Convert Thrift request to Protobuf
        ::InstallSnapshotRequest proto_req = to_protobuf(req);
        ::InstallSnapshotResponse proto_resp;

        // Create Controller and Closure
        Controller cntl;
        SyncClosure done;
        brpc::ClosureGuard done_guard(&done);

        // Call legacy Node's handle_install_snapshot_request
        _node_legacy->handle_install_snapshot_request(&cntl, &proto_req, &proto_resp, &done);

        // Convert Protobuf response back to Thrift
        from_protobuf(proto_resp, _return);

        // Check for errors
        if (cntl.Failed()) {
            _return.__set_success(false);
        }
    }
}

void RaftRpcService::timeoutNow(TimeoutNowResponse& _return,
                                 const TimeoutNowRequest& req) {
    if (_use_v2 && _node_v2) {
        _node_v2->handleTimeoutNow(req, _return);
    } else if (_node_legacy) {
        // Convert Thrift request to Protobuf
        ::TimeoutNowRequest proto_req = to_protobuf(req);
        ::TimeoutNowResponse proto_resp;

        // Create Controller and Closure
        Controller cntl;
        SyncClosure done;
        brpc::ClosureGuard done_guard(&done);

        // Call legacy Node's handle_timeout_now_request
        _node_legacy->handle_timeout_now_request(&cntl, &proto_req, &proto_resp, &done);

        // Convert Protobuf response back to Thrift
        from_protobuf(proto_resp, _return);

        // Check for errors
        if (cntl.Failed()) {
            _return.__set_success(false);
        }
    }
}

} // namespace braft
