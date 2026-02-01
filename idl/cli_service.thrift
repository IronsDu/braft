// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// CLI service for cluster management
// Maps to braft/cli.proto

namespace cpp braft

// ============================================================
// Add Peer Operations
// ============================================================

/**
 * AddPeerRequest - Add a single peer to the cluster
 */
struct AddPeerRequest {
  1: string group_id,   // Raft group identifier
  2: string leader_id,  // Leader's peer ID
  3: string peer_id     // Peer ID to add (format: "ip:port:index")
}

/**
 * AddPeerResponse - Result of add_peer operation
 */
struct AddPeerResponse {
  1: list<string> old_peers,  // Peer list before change
  2: list<string> new_peers   // Peer list after change
}

// ============================================================
// Remove Peer Operations
// ============================================================

/**
 * RemovePeerRequest - Remove a single peer from the cluster
 */
struct RemovePeerRequest {
  1: string group_id,   // Raft group identifier
  2: string leader_id,  // Leader's peer ID
  3: string peer_id     // Peer ID to remove
}

/**
 * RemovePeerResponse - Result of remove_peer operation
 */
struct RemovePeerResponse {
  1: list<string> old_peers,  // Peer list before change
  2: list<string> new_peers   // Peer list after change
}

// ============================================================
// Change Peers Operations
// ============================================================

/**
 * ChangePeersRequest - Replace entire peer configuration
 */
struct ChangePeersRequest {
  1: string group_id,          // Raft group identifier
  2: string leader_id,         // Leader's peer ID
  3: list<string> new_peers    // New peer list to set
}

/**
 * ChangePeersResponse - Result of change_peers operation
 */
struct ChangePeersResponse {
  1: list<string> old_peers,  // Peer list before change
  2: list<string> new_peers   // Peer list after change
}

// ============================================================
// Reset Peer Operations
// ============================================================

/**
 * ResetPeerRequest - Reset peer configuration (used for recovery)
 * Forces a node to adopt a specific configuration
 */
struct ResetPeerRequest {
  1: string group_id,          // Raft group identifier
  2: string peer_id,           // Peer ID to reset
  3: list<string> old_peers,   // Old configuration
  4: list<string> new_peers    // New configuration to adopt
}

/**
 * ResetPeerResponse - Result of reset_peer operation
 */
struct ResetPeerResponse {}

// ============================================================
// Snapshot Operations
// ============================================================

/**
 * SnapshotRequest - Trigger snapshot creation
 */
struct SnapshotRequest {
  1: string group_id,          // Raft group identifier
  2: optional string peer_id   // Specific peer ID (empty for leader)
}

/**
 * SnapshotResponse - Result of snapshot operation
 */
struct SnapshotResponse {}

// ============================================================
// Transfer Leader Operations
// ============================================================

/**
 * TransferLeaderRequest - Transfer leadership to another peer
 */
struct TransferLeaderRequest {
  1: string group_id,          // Raft group identifier
  2: string leader_id,         // Current leader's peer ID
  3: optional string peer_id   // Target peer ID (empty for automatic selection)
}

/**
 * TransferLeaderResponse - Result of transfer_leader operation
 */
struct TransferLeaderResponse {}

// ============================================================
// Get Leader Operations
// ============================================================

/**
 * GetLeaderRequest - Query current leader
 */
struct GetLeaderRequest {
  1: string group_id,          // Raft group identifier
  2: optional string peer_id   // Specific peer to query (empty for any)
}

/**
 * GetLeaderResponse - Current leader information
 */
struct GetLeaderResponse {
  1: string leader_id  // Current leader's peer ID
}

// ============================================================
// CLI Service Definition
// ============================================================

/**
 * CliService - Cluster management service
 *
 * This service provides administrative operations for Raft cluster management:
 * - Add/remove/change peers (configuration changes)
 * - Trigger snapshots
 * - Transfer leadership
 * - Query cluster state
 *
 * All operations should be sent to the current leader, except get_leader
 * which can be sent to any peer.
 *
 * fbthrift generates coroutine-based methods (co_*) for async operations.
 */
service CliService {
  /**
   * addPeer - Add a new peer to the cluster
   * Initiates a joint consensus configuration change
   */
  AddPeerResponse addPeer(1: AddPeerRequest req),

  /**
   * removePeer - Remove a peer from the cluster
   * Initiates a joint consensus configuration change
   */
  RemovePeerResponse removePeer(1: RemovePeerRequest req),

  /**
   * changePeers - Replace the entire peer configuration
   * More efficient than multiple add/remove operations
   */
  ChangePeersResponse changePeers(1: ChangePeersRequest req),

  /**
   * resetPeer - Force a peer to adopt a specific configuration
   * Used for recovery from split-brain scenarios
   * WARNING: Use with caution, can cause data loss
   */
  ResetPeerResponse resetPeer(1: ResetPeerRequest req),

  /**
   * snapshot - Trigger snapshot creation on specified peer
   * If peer_id is empty, creates snapshot on leader
   */
  SnapshotResponse snapshot(1: SnapshotRequest req),

  /**
   * getLeader - Query the current leader of the group
   * Can be sent to any peer in the group
   */
  GetLeaderResponse getLeader(1: GetLeaderRequest req),

  /**
   * transferLeader - Transfer leadership to another peer
   * If peer_id is empty, leader selects a suitable follower
   */
  TransferLeaderResponse transferLeader(1: TransferLeaderRequest req)
}
