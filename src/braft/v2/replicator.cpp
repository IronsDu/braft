// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/v2/replicator.h"
#include "braft/v2/node.h"
#include "braft/v2/log_manager.h"

#include <chrono>
#include <iostream>
#include <algorithm>
#include <sstream>

namespace braft {
namespace v2 {

Replicator::Replicator(const std::string& peer_addr,
                       Node* node,
                       int64_t term)
    : _peer_addr(peer_addr),
      _node(node),
      _term(term),
      _running(false),
      _stop_heartbeat(false),
      _heartbeat_timeout_ms(100),
      _max_retry(3),
      _next_index(1),
      _match_index(0),
      _last_commit_index(0),
      _last_heartbeat_time(0),
      _success_count(0),
      _failure_count(0) {
    _rpc_client.reset(new RaftRpcClient(_peer_addr));
}

Replicator::~Replicator() {
    stop();
}

bool Replicator::start() {
    if (_running.load()) {
        return false;
    }

    // Connect to peer
    if (!_rpc_client->connect()) {
        std::cerr << "Failed to connect to peer: " << _peer_addr << std::endl;
        return false;
    }

    _running.store(true);
    _stop_heartbeat.store(false);

    // Start heartbeat thread
    _thread.reset(new std::thread([this]() {
        this->heartbeatLoop();
    }));

    return true;
}

void Replicator::stop() {
    if (!_running.load()) {
        return;
    }

    _running.store(false);
    _stop_heartbeat.store(true);

    if (_rpc_client) {
        _rpc_client->close();
    }
}

void Replicator::join() {
    if (_thread && _thread->joinable()) {
        _thread->join();
    }
}

AppendEntriesResponse Replicator::sendAppendEntries(const AppendEntriesRequest& req) {
    AppendEntriesResponse response;

    if (!_running.load() || !_rpc_client->isConnected()) {
        response.success = false;
        return response;
    }

    try {
        int retry = 0;
        while (retry < _max_retry) {
            response = _rpc_client->appendEntries(req);
            if (response.success) {
                _success_count.fetch_add(1);
                return response;
            }
            retry++;
        }
        _failure_count.fetch_add(1);
    } catch (const std::exception& e) {
        std::cerr << "Failed to send AppendEntries to " << _peer_addr
                  << ": " << e.what() << std::endl;
        _failure_count.fetch_add(1);
    }

    response.success = false;
    return response;
}

InstallSnapshotResponse Replicator::sendInstallSnapshot(const InstallSnapshotRequest& req) {
    InstallSnapshotResponse response;

    if (!_running.load() || !_rpc_client->isConnected()) {
        response.success = false;
        return response;
    }

    try {
        int retry = 0;
        while (retry < _max_retry) {
            response = _rpc_client->installSnapshot(req);
            if (response.success) {
                _success_count.fetch_add(1);
                return response;
            }
            retry++;
        }
        _failure_count.fetch_add(1);
    } catch (const std::exception& e) {
        std::cerr << "Failed to send InstallSnapshot to " << _peer_addr
                  << ": " << e.what() << std::endl;
        _failure_count.fetch_add(1);
    }

    response.success = false;
    return response;
}

void Replicator::sendHeartbeat() {
    AppendEntriesRequest request;
    request.term = _term;
    request.server_id = _node->getServerId();
    request.prev_log_index = 0;
    request.prev_log_term = 0;
    // Empty entries for heartbeat
    request.entries = std::vector<EntryMeta>();
    request.committed_index = _node->getCommitIndex();

    try {
        AppendEntriesResponse response = sendAppendEntries(request);
        _last_heartbeat_time.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    } catch (const std::exception& e) {
        std::cerr << "Failed to send heartbeat to " << _peer_addr
                  << ": " << e.what() << std::endl;
    }
}

void Replicator::run() {
    // Main replication loop
    // NOTE: Currently heartbeatLoop() handles replication
    // This method is reserved for future use (e.g., async replication with folly::coro)
    while (_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Replicator::heartbeatLoop() {
    while (_running.load() && !_stop_heartbeat.load()) {
        // Try to replicate logs first
        replicateLogs();

        // Then send heartbeat if nothing to replicate
        // (replicateLogs() will send heartbeat internally if no logs pending)

        // Sleep for heartbeat interval
        std::this_thread::sleep_for(std::chrono::milliseconds(_heartbeat_timeout_ms));
    }
}

void Replicator::replicateLogs() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_running.load() || !_node) {
        return;
    }

    LogManager* log_manager = _node->getLogManager();
    if (!log_manager) {
        return;
    }

    int64_t last_log_index = log_manager->lastLogIndex();

    // If nothing to replicate, just send heartbeat
    if (_next_index > last_log_index) {
        sendHeartbeat();
        return;
    }

    // Build AppendEntries request
    AppendEntriesRequest request;
    request.term = _term;
    request.server_id = _node->getServerId();

    // prev_log_index is the index before next_index
    int64_t prev_log_index = _next_index - 1;
    request.prev_log_index = prev_log_index;

    // Get prev_log_term from log manager
    if (prev_log_index > 0) {
        request.prev_log_term = log_manager->getTerm(prev_log_index);
    } else {
        request.prev_log_term = 0;
    }

    // Collect entries to send (batch size could be configurable)
    std::vector<EntryMeta> entries;
    int64_t max_batch_size = 100;  // Maximum entries per RPC

    for (int64_t i = _next_index; i <= last_log_index && entries.size() < max_batch_size; ++i) {
        auto entry = log_manager->getEntry(i);
        if (entry) {
            EntryMeta meta;
            meta.term = entry->term;
            meta.type = static_cast<ThriftEntryType::type>(entry->type);
            meta.data_len = entry->data.size();
            // Note: EntryMeta only has metadata, actual data would be in attachment
            // For this simplified version, we don't transfer actual data
            entries.push_back(meta);
        }
    }

    request.entries = entries;

    // Set committed index
    request.committed_index = _node->getCommitIndex();

    // Send AppendEntries
    AppendEntriesResponse response = sendAppendEntries(request);

    // Handle response
    if (response.success) {
        // Update match_index and next_index
        if (!entries.empty()) {
            _match_index = prev_log_index + entries.size();
            _next_index = _match_index + 1;

            std::cout << "Replicated " << entries.size() << " entries to "
                      << _peer_addr << " (next_index=" << _next_index
                      << ", match_index=" << _match_index << ")" << std::endl;
        }

        // Update commit index if follower has more info
        if (response.__isset.last_log_index) {
            // Could use this to optimize commit propagation
        }
    } else {
        // Log inconsistency: decrement next_index and retry
        if (_next_index > 1) {
            _next_index--;

            // Check if we need to send snapshot
            // Get snapshot info from LogManager
            auto snapshot_info = log_manager->getSnapshot();
            int64_t snapshot_index = snapshot_info.first;

            if (_next_index <= snapshot_index) {
                // Follower is too far behind, send snapshot
                std::cout << "Node " << _peer_addr
                          << " is too far behind (next_index=" << _next_index
                          << ", snapshot_index=" << snapshot_index
                          << "), sending snapshot" << std::endl;

                sendSnapshotToPeer(snapshot_info);
            } else {
                std::cout << "Log inconsistency with " << _peer_addr
                          << ", decremented next_index to " << _next_index << std::endl;
            }
        }
    }
}

void Replicator::onCommitIndexChange(int64_t new_commit_index) {
    std::lock_guard<std::mutex> lock(_mutex);
    _last_commit_index = new_commit_index;

    // Trigger immediate replication to notify followers
    // Note: In production, this would be async
    if (_running.load()) {
        // Could trigger a condition variable to wake up replication thread
    }
}

void Replicator::sendSnapshotToPeer(const std::pair<int64_t, int64_t>& snapshot_info) {
    int64_t last_included_index = snapshot_info.first;
    int64_t last_included_term = snapshot_info.second;

    // Build InstallSnapshot request
    InstallSnapshotRequest request;
    request.term = _term;
    request.server_id = _node->getServerId();

    // Set snapshot metadata
    request.meta.last_included_index = last_included_index;
    request.meta.last_included_term = last_included_term;

    // Set snapshot URI (in production, this would be a file path or URL)
    // For this simplified version, we use a placeholder
    request.uri = "snapshot://" + _node->getServerId() + "/" +
                  std::to_string(last_included_index);

    // Send InstallSnapshot RPC
    InstallSnapshotResponse response = sendInstallSnapshot(request);

    if (response.success) {
        // Update next_index and match_index
        _next_index = last_included_index + 1;
        _match_index = last_included_index;

        std::cout << "Snapshot sent to " << _peer_addr
                  << " (next_index=" << _next_index
                  << ", match_index=" << _match_index << ")" << std::endl;
    }
}

} // namespace v2
} // namespace braft
