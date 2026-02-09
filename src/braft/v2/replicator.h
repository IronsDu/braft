// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Replicator v2 - Thrift-based log replication

#ifndef BRAFT_V2_REPLICATOR_H
#define BRAFT_V2_REPLICATOR_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

#include "braft/rpc/raft_rpc_client.h"

// Thrift types
#include "raft_types.h"

namespace braft {
namespace v2 {

// Forward declarations
class Node;

/**
 * ReplicatorOptions - Configuration for Replicator
 */
struct ReplicatorOptions {
    std::string peer_addr;      // Follower's address (host:port)
    int heartbeat_timeout_ms;   // Heartbeat interval
    int election_timeout_ms;   // Election timeout
    int max_retry;              // Maximum retry attempts
};

/**
 * Replicator - Log replication using Thrift RPC
 *
 * This is a simplified version of the replicator that:
 * - Sends AppendEntries RPC to followers
 * - Sends periodic heartbeats
 * - Handles retries and timeouts
 *
 * TODO: In production, this should use folly::coro for async operations
 */
class Replicator : public std::enable_shared_from_this<Replicator> {
public:
    /**
     * Constructor
     * @param peer_addr Follower's address
     * @param node Leader node pointer
     * @param term Current term
     */
    Replicator(const std::string& peer_addr,
               Node* node,
               int64_t term);

    ~Replicator();

    // ========== Lifecycle ==========

    /**
     * Start the replicator thread
     * @return true on success
     */
    bool start();

    /**
     * Stop the replicator thread
     */
    void stop();

    /**
     * Wait for the replicator thread to finish
     */
    void join();

    /**
     * Check if running
     */
    bool isRunning() const { return _running; }

    // ========== Operations ==========

    /**
     * Send append entries request
     * @param req AppendEntries request
     * @return Response from follower
     */
    AppendEntriesResponse sendAppendEntries(const AppendEntriesRequest& req);

    /**
     * Send install snapshot request
     * @param req InstallSnapshot request
     * @return Response from follower
     */
    InstallSnapshotResponse sendInstallSnapshot(const InstallSnapshotRequest& req);

    /**
     * Trigger a heartbeat
     */
    void sendHeartbeat();

    /**
     * Trigger log replication (send pending entries)
     */
    void replicateLogs();

    /**
     * Notify that commit index has changed
     */
    void onCommitIndexChange(int64_t new_commit_index);

    /**
     * Send snapshot to follower
     * @param snapshot_info Pair of (last_included_index, last_included_term)
     */
    void sendSnapshotToPeer(const std::pair<int64_t, int64_t>& snapshot_info);

    // ========== Accessors ==========

    /**
     * Get peer address
     */
    const std::string& getPeerAddr() const { return _peer_addr; }

    /**
     * Get current term
     */
    int64_t getTerm() const { return _term; }

    /**
     * Set term
     */
    void setTerm(int64_t term) { _term = term; }

    /**
     * Get next index to send
     */
    int64_t getNextIndex() const { return _next_index; }

    /**
     * Set next index
     */
    void setNextIndex(int64_t index) { _next_index = index; }

    /**
     * Get match index
     */
    int64_t getMatchIndex() const { return _match_index; }

    /**
     * Set match index
     */
    void setMatchIndex(int64_t index) { _match_index = index; }

private:
    /**
     * Replicator thread main loop
     */
    void run();

    /**
     * Send heartbeat loop
     */
    void heartbeatLoop();

private:
    std::string _peer_addr;
    Node* _node;  // Not owned
    int64_t _term;

    // RPC client
    std::unique_ptr<RaftRpcClient> _rpc_client;

    // Thread management
    std::unique_ptr<std::thread> _thread;
    std::atomic<bool> _running;
    std::atomic<bool> _stop_heartbeat;

    // Options
    int _heartbeat_timeout_ms;
    int _max_retry;

    // Log replication state
    int64_t _next_index;     // Next log index to send to this peer
    int64_t _match_index;    // Highest log index known to be replicated
    int64_t _last_commit_index;  // Last notified commit index

    // Statistics
    std::atomic<int64_t> _last_heartbeat_time;
    std::atomic<int64_t> _success_count;
    std::atomic<int64_t> _failure_count;

    mutable std::mutex _mutex;
};

} // namespace v2
} // namespace braft

#endif // BRAFT_V2_REPLICATOR_H
