// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Basic types and enumerations for Raft protocol

namespace cpp braft

/**
 * EntryType - Log entry type enumeration
 * Maps to braft/enum.proto EntryType
 */
enum EntryType {
  ENTRY_TYPE_UNKNOWN = 0,
  ENTRY_TYPE_NO_OP = 1,
  ENTRY_TYPE_DATA = 2,
  ENTRY_TYPE_CONFIGURATION = 3
}

/**
 * ErrorType - Error type enumeration
 * Maps to braft/enum.proto ErrorType
 */
enum ErrorType {
  ERROR_TYPE_NONE = 0,
  ERROR_TYPE_LOG = 1,
  ERROR_TYPE_STABLE = 2,
  ERROR_TYPE_SNAPSHOT = 3,
  ERROR_TYPE_STATE_MACHINE = 4
}

/**
 * RaftError - Raft specific error codes
 * Maps to braft/errno.proto RaftError
 * Error codes start from 10000 to avoid conflicts with HTTP and RPC errors
 */
enum RaftError {
  ERAFTTIMEDOUT = 10001,         // Various timeouts (election, timeout_now, stepdown)
  ESTATEMACHINE = 10002,         // Bad user state machine
  ECATCHUP = 10003,              // Catchup failed
  ELEADERREMOVED = 10004,        // Leader removed after configuration change
  ESETPEER = 10005,              // Set peer operation
  ENODESHUTDOWN = 10006,         // Node shutdown
  EHIGHERTERMREQUEST = 10007,    // Received request with higher term
  EHIGHERTERMRESPONSE = 10008,   // Received response with higher term
  EBADNODE = 10009,              // Node is in error state
  EVOTEFORCANDIDATE = 10010,     // Node voted for a candidate
  ENEWLEADER = 10011,            // Received heartbeat from new leader
  ELEADERCONFLICT = 10012,       // Multiple leaders in same term
  ETRANSFERLEADERSHIP = 10013,   // Leader transferred leadership
  ELOGDELETED = 10014,           // Log at given index was deleted
  ENOMOREUSERLOG = 10015,        // No more user logs to read
  EREADONLY = 10016              // Raft node in readonly mode
}

/**
 * TermLeader - Represents a leader in a specific term
 * Used for disrupted leader detection
 */
struct TermLeader {
  1: string peer_id,
  2: i64 term
}

/**
 * EntryMeta - Metadata for a log entry
 * Stored in brpc attachment for efficiency
 */
struct EntryMeta {
  1: i64 term,
  2: EntryType type,
  3: list<string> peers,
  4: optional i64 data_len,
  // old_peers field ID preserved for backward compatibility consideration
  5: list<string> old_peers
}

/**
 * SnapshotMeta - Metadata for a snapshot
 * Contains the last included index/term and configuration
 */
struct SnapshotMeta {
  1: i64 last_included_index,
  2: i64 last_included_term,
  3: list<string> peers,
  4: list<string> old_peers
}
