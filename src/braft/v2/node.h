// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Node v2 - Thrift-based implementation

#ifndef BRAFT_V2_NODE_H
#define BRAFT_V2_NODE_H

#include <string>
#include <memory>
#include <vector>
#include <mutex>

// Thrift types
#include "RaftService.h"
#include "raft_types.h"

#include "braft/rpc/thrift_server.h"
#include "braft/rpc/raft_rpc_service.h"
#include "braft/v2/replicator.h"
#include "braft/v2/log_manager.h"
#include "braft/v2/fsm_caller.h"
#include "braft/v2/election_timer.h"

namespace braft {
namespace v2 {

// Forward declarations
class Replicator;
class LogManager;
class FSMCaller;
class ElectionTimer;

/**
 * Node - Raft node implementation using Thrift
 *
 * This is a simplified version demonstrating the v2 architecture.
 * The full implementation would include:
 * - Leader election
 * - Log replication
 * - Snapshot management
 * - Configuration changes
 */
class Node : public std::enable_shared_from_this<Node> {
public:
    /**
     * Constructor
     * @param group_id Raft group identifier
     * @param server_id Server's own peer ID (format: "ip:port:index")
     * @param peers Initial peer list
     */
    Node(const std::string& group_id,
         const std::string& server_id,
         const std::vector<std::string>& peers);

    ~Node();

    // ========== Lifecycle Management ==========

    /**
     * Start the node
     * @param port Port to listen on
     * @return true on success
     */
    bool start(int port);

    /**
     * Shutdown the node gracefully
     */
    void shutdown();

    /**
     * Check if node is running
     */
    bool isRunning() const { return _running; }

    // ========== Accessors ==========

    /**
     * Get node's current term
     */
    int64_t getCurrentTerm() const { return _current_term; }

    /**
     * Get node's state (LEADER, FOLLOWER, CANDIDATE)
     * Note: Simplified, actual implementation would use enum
     */
    const std::string& getState() const { return _state; }

    /**
     * Get group ID
     */
    const std::string& getGroupId() const { return _group_id; }

    /**
     * Get server ID
     */
    const std::string& getServerId() const { return _server_id; }

    /**
     * Get log manager
     */
    LogManager* getLogManager() { return _log_manager.get(); }

    /**
     * Get FSM caller
     */
    FSMCaller* getFSMCaller() { return _fsm_caller.get(); }

    /**
     * Get commit index
     */
    int64_t getCommitIndex() const { return _commit_index; }

    // ========== RPC Handlers (called by RaftRpcService) ==========

    /**
     * Handle pre-vote request
     */
    void handlePreVote(const RequestVoteRequest& req,
                       RequestVoteResponse& resp);

    /**
     * Handle request vote request
     */
    void handleRequestVote(const RequestVoteRequest& req,
                           RequestVoteResponse& resp);

    /**
     * Handle append entries request
     */
    void handleAppendEntries(const AppendEntriesRequest& req,
                             AppendEntriesResponse& resp);

    /**
     * Handle install snapshot request
     */
    void handleInstallSnapshot(const InstallSnapshotRequest& req,
                               InstallSnapshotResponse& resp);

    /**
     * Handle timeout now request
     */
    void handleTimeoutNow(const TimeoutNowRequest& req,
                          TimeoutNowResponse& resp);

    // ========== Leader/Follower Management ==========

    /**
     * Become leader and start replicating to followers
     */
    void becomeLeader();

    /**
     * Step down from leadership
     */
    void stepDown();

    /**
     * Check if node is leader
     */
    bool isLeader() const { return _state == "LEADER"; }

    // ========== Client Operations ==========

    /**
     * Propose a new entry to the Raft log
     * @param data Data to propose
     * @return Log index if successful, <= 0 if failed (not leader)
     */
    int64_t propose(const std::vector<uint8_t>& data);

    /**
     * Trigger replication to all followers
     */
    void triggerReplication();

    /**
     * Update commit index based on replicators' match_index
     */
    void updateCommitIndex();

    /**
     * Create a snapshot at current commit index
     * @return true if snapshot creation was triggered
     */
    bool createSnapshot();

    // ========== Configuration Change ==========

    /**
     * Add a peer to the cluster
     * @param peer_id Peer to add (format: "ip:port:index")
     * @return Log index if successful, <= 0 if failed
     */
    int64_t addPeer(const std::string& peer_id);

    /**
     * Remove a peer from the cluster
     * @param peer_id Peer to remove
     * @return Log index if successful, <= 0 if failed
     */
    int64_t removePeer(const std::string& peer_id);

    /**
     * Apply configuration change (called when config log is committed)
     * @param new_peers New peer list
     */
    void applyConfiguration(const std::vector<std::string>& new_peers);

    // ========== Election ==========

    /**
     * Start election (called by election timer or manually)
     */
    void startElection();

private:
    /**
     * Election timeout callback
     */
    void onElectionTimeout();

    /**
     * Send RequestVote to all peers
     */
    void sendRequestVote();

    /**
     * Handle vote response
     */
    void handleVoteResponse(const std::string& peer_id,
                           const RequestVoteResponse& response);

    /**
     * Check if won election
     */
    bool checkElectionWon();

    /**
     * Internal becomeLeader without locking (must be called with mutex held)
     */
    void becomeLeaderInternal();

private:
    // ========== Member Variables ==========

    std::string _group_id;
    std::string _server_id;
    std::vector<std::string> _peers;

    // Raft state
    int64_t _current_term;
    std::string _voted_for;
    std::string _leader_id;  // Current leader's server_id
    std::string _state;  // "LEADER", "FOLLOWER", "CANDIDATE"

    // Log state (simplified - in production would use LogManager)
    int64_t _last_log_index;
    int64_t _last_log_term;
    int64_t _commit_index;

    // Server components
    std::unique_ptr<ThriftServer> _server;
    std::shared_ptr<RaftRpcService> _raft_service;

    // Replication (only used when leader)
    std::vector<std::shared_ptr<Replicator>> _replicators;

    // Core modules
    std::unique_ptr<LogManager> _log_manager;
    std::unique_ptr<FSMCaller> _fsm_caller;
    std::unique_ptr<ElectionTimer> _election_timer;

    // Election state
    int _votes_granted;           // Number of votes granted
    int _votes_refused;           // Number of votes refused
    std::atomic<bool> _election_complete;  // Election completed flag

    // Synchronization
    mutable std::mutex _mutex;
    std::atomic<bool> _running;
};

} // namespace v2
} // namespace braft

#endif // BRAFT_V2_NODE_H
