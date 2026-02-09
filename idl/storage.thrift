// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Local storage metadata structures
// Maps to braft/local_storage.proto

include "base.thrift"

namespace cpp braft

/**
 * ConfigurationPBMeta - Configuration metadata stored on disk
 * Represents a single configuration (either old or new in joint consensus)
 */
struct ConfigurationPBMeta {
  1: list<string> peers,     // List of peer IDs in this configuration
  2: list<string> old_peers  // Old configuration (for joint consensus)
}

/**
 * LogPBMeta - Log file metadata
 * Stored in log file header
 */
struct LogPBMeta {
  1: i64 first_log_index  // Index of the first log entry in this file
}

/**
 * StablePBMeta - Stable storage metadata (persistent state)
 * Stored in stable storage (typically disk)
 * Contains the two persistent fields from Raft paper:
 * - currentTerm
 * - votedFor
 */
struct StablePBMeta {
  1: i64 term,         // Latest term server has seen
  2: string votedfor   // CandidateId that received vote in current term
}

/**
 * LocalFileMeta - Metadata for a single file in snapshot
 * Contains file-specific metadata like checksum, size, etc.
 * TODO: Add actual fields based on implementation needs
 */
struct LocalFileMeta {
  // Placeholder for future metadata fields
  // Potential fields:
  // 1: i64 file_size,
  // 2: string checksum,
  // 3: i64 mtime,
  // etc.
}

/**
 * LocalFile - Represents a single file in a snapshot
 */
struct LocalFile {
  1: string name,                  // File name or relative path
  2: optional LocalFileMeta meta   // File metadata
}

/**
 * LocalSnapshotPbMeta - Local snapshot metadata
 * Stored in snapshot meta file
 * Contains information about files included in the snapshot
 */
struct LocalSnapshotPbMeta {
  1: optional base.SnapshotMeta meta,  // Raft snapshot metadata
  2: list<LocalFile> files             // List of files in this snapshot
}
