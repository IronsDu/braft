// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Core Raft RPC protocol definitions
// Maps to braft/raft.proto

include "base.thrift"

namespace cpp braft

/**
 * RequestVoteRequest - RequestVote RPC parameters
 * Used for both pre-vote and actual vote phases
 */
struct RequestVoteRequest {
  1: string group_id,          // Raft group identifier
  2: string server_id,         // Server ID of the candidate
  3: string peer_id,           // Peer ID of the candidate
  4: i64 term,                 // Candidate's term
  5: i64 last_log_term,        // Term of candidate's last log entry
  6: i64 last_log_index,       // Index of candidate's last log entry
  7: optional base.TermLeader disrupted_leader  // For disrupted leader detection
}

/**
 * RequestVoteResponse - RequestVote RPC response
 */
struct RequestVoteResponse {
  1: i64 term,                 // Current term (for candidate to update itself)
  2: bool granted,             // True means candidate received vote
  3: optional bool disrupted,  // For disrupted leader detection
  4: optional i64 previous_term, // For disrupted leader detection
  5: optional bool rejected_by_lease  // Rejected due to leader lease
}

/**
 * AppendEntriesRequest - AppendEntries RPC parameters
 * Used for log replication and heartbeat
 */
struct AppendEntriesRequest {
  1: string group_id,          // Raft group identifier
  2: string server_id,         // Server ID of the leader
  3: string peer_id,           // Peer ID of the follower
  4: i64 term,                 // Leader's term
  5: i64 prev_log_term,        // Term of prevLogIndex entry
  6: i64 prev_log_index,       // Index of log entry immediately preceding new ones
  7: list<base.EntryMeta> entries,  // Log entries to store (empty for heartbeat)
  8: i64 committed_index       // Leader's committed index
}

/**
 * AppendEntriesResponse - AppendEntries RPC response
 */
struct AppendEntriesResponse {
  1: i64 term,                 // Current term (for leader to update itself)
  2: bool success,             // True if follower contained entry matching prevLogIndex
  3: optional i64 last_log_index,  // Follower's last log index (for optimization)
  4: optional bool readonly    // True if request is read-only
}

/**
 * InstallSnapshotRequest - InstallSnapshot RPC parameters
 * Used to send snapshot to lagging followers
 */
struct InstallSnapshotRequest {
  1: string group_id,          // Raft group identifier
  2: string server_id,         // Server ID of the leader
  3: string peer_id,           // Peer ID of the follower
  4: i64 term,                 // Leader's term
  5: base.SnapshotMeta meta,   // Snapshot metadata
  6: string uri                // URI to download snapshot file
}

/**
 * InstallSnapshotResponse - InstallSnapshot RPC response
 */
struct InstallSnapshotResponse {
  1: i64 term,                 // Current term (for leader to update itself)
  2: bool success              // True if snapshot installed successfully
}

/**
 * TimeoutNowRequest - TimeoutNow RPC parameters
 * Used to force a follower to start election immediately
 */
struct TimeoutNowRequest {
  1: string group_id,          // Raft group identifier
  2: string server_id,         // Server ID of the leader
  3: string peer_id,           // Peer ID of the follower
  4: i64 term,                 // Leader's term
  5: optional bool old_leader_stepped_down  // Whether old leader has stepped down
}

/**
 * TimeoutNowResponse - TimeoutNow RPC response
 */
struct TimeoutNowResponse {
  1: i64 term,                 // Current term
  2: bool success              // True if follower started election
}

/**
 * RaftService - Core Raft RPC service
 * All methods support C++20 coroutines via fbthrift
 *
 * fbthrift will generate coroutine-based methods:
 * - co_preVote(), co_requestVote(), etc.
 * - Use folly::coro::Task<T> as return type for async operations
 */
service RaftService {
  /**
   * preVote - Pre-vote phase to avoid disruption
   * Candidate asks if it would receive vote without incrementing term
   */
  RequestVoteResponse preVote(1: RequestVoteRequest req),

  /**
   * requestVote - Actual vote phase
   * Candidate requests vote from followers
   */
  RequestVoteResponse requestVote(1: RequestVoteRequest req),

  /**
   * appendEntries - Log replication and heartbeat
   * Leader sends log entries to followers
   * Also used as heartbeat when entries is empty
   */
  AppendEntriesResponse appendEntries(1: AppendEntriesRequest req),

  /**
   * installSnapshot - Send snapshot to follower
   * Used when follower is too far behind
   */
  InstallSnapshotResponse installSnapshot(1: InstallSnapshotRequest req),

  /**
   * timeoutNow - Force immediate election
   * Used for leader transfer
   */
  TimeoutNowResponse timeoutNow(1: TimeoutNowRequest req)
}
