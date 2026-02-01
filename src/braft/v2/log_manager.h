// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// LogManager v2 - Simplified log storage and management

#ifndef BRAFT_V2_LOG_MANAGER_H
#define BRAFT_V2_LOG_MANAGER_H

#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <atomic>

#include "raft_types.h"

namespace braft {
namespace v2 {

/**
 * LogEntry - Log entry with data and metadata
 */
struct LogEntry {
    int64_t index;
    int64_t term;
    int32_t type;  // EntryType enum value
    std::vector<uint8_t> data;

    LogEntry() : index(0), term(0), type(0) {}
};

/**
 * LogManagerOptions - Configuration for LogManager
 */
struct LogManagerOptions {
    std::string data_dir;      // Directory for log storage
    size_t max_memory_logs;    // Maximum logs in memory
    bool enable_persistence;   // Enable disk persistence

    LogManagerOptions()
        : max_memory_logs(1000),
          enable_persistence(false) {}
};

/**
 * LogManager - Simplified log storage and management
 *
 * This is a simplified version of LogManager that:
 * - Stores log entries in memory
 * - Supports optional disk persistence
 * - Provides log retrieval and term queries
 * - Manages log indexing
 *
 * TODO: In production, this should use:
 * - folly::coro for async operations
 * - Proper WAL (Write-Ahead Log) for persistence
 * - Log compaction and snapshot integration
 */
class LogManager {
public:
    LogManager();
    ~LogManager();

    /**
     * Initialize the log manager
     * @param options Configuration options
     * @return true on success
     */
    bool init(const LogManagerOptions& options);

    /**
     * Shutdown the log manager
     */
    void shutdown();

    // ========== Log Operations ==========

    /**
     * Append a log entry
     * @param entry Log entry to append
     * @return Index of appended entry, -1 on failure
     */
    int64_t appendEntry(const LogEntry& entry);

    /**
     * Append multiple log entries
     * @param entries Log entries to append
     * @return Index of last appended entry, -1 on failure
     */
    int64_t appendEntries(const std::vector<LogEntry>& entries);

    /**
     * Get log entry at index
     * @param index Log index
     * @return Log entry, or nullptr if not found
     */
    std::shared_ptr<LogEntry> getEntry(int64_t index);

    /**
     * Get term at index
     * @param index Log index
     * @return Term, or 0 if not found
     */
    int64_t getTerm(int64_t index);

    // ========== Index Management ==========

    /**
     * Get first log index
     * @return First index, or 0 if empty
     */
    int64_t firstLogIndex() const;

    /**
     * Get last log index
     * @return Last index, or 0 if empty
     */
    int64_t lastLogIndex() const;

    /**
     * Get last log term
     * @return Last term, or 0 if empty
     */
    int64_t lastLogTerm() const;

    /**
     * Get last log ID (index, term)
     * @return Pair of (index, term)
     */
    std::pair<int64_t, int64_t> lastLogId() const;

    // ========== Truncation ==========

    /**
     * Truncate logs from beginning (keep [first_index_kept, ...])
     * @param first_index_kept First index to keep
     * @return true on success
     */
    bool truncatePrefix(int64_t first_index_kept);

    /**
     * Truncate logs from end (keep [..., last_index_kept])
     * @param last_index_kept Last index to keep
     * @return true on success
     */
    bool truncateSuffix(int64_t last_index_kept);

    // ========== Snapshot Support ==========

    /**
     * Set the snapshot metadata
     * This allows truncating logs before the snapshot
     * @param last_included_index Last index included in snapshot
     * @param last_included_term Term of last included index
     */
    void setSnapshot(int64_t last_included_index, int64_t last_included_term);

    /**
     * Get snapshot metadata
     * @return Pair of (last_included_index, last_included_term)
     */
    std::pair<int64_t, int64_t> getSnapshot() const;

    // ========== Statistics ==========

    /**
     * Get number of logs in memory
     */
    size_t getLogCount() const;

    /**
     * Check if log manager is empty
     */
    bool isEmpty() const;

    /**
     * Check if running
     */
    bool isRunning() const { return _running; }

private:
    /**
     * Persist log to disk (simplified)
     */
    void persistEntry(const LogEntry& entry);

    /**
     * Load logs from disk on startup (simplified)
     */
    bool loadFromDisk();

    /**
     * Check if index is valid
     */
    bool isValidIndex(int64_t index) const;

    /**
     * Internal truncate prefix without locking (must be called with lock held)
     */
    void truncatePrefixInternal(int64_t first_index_kept);

private:
    LogManagerOptions _options;

    // Log storage (in-memory)
    std::deque<LogEntry> _logs;

    // Index management
    int64_t _first_log_index;
    int64_t _last_log_index;

    // Snapshot info
    int64_t _snapshot_index;
    int64_t _snapshot_term;

    // Synchronization
    mutable std::mutex _mutex;
    std::atomic<bool> _running;
};

} // namespace v2
} // namespace braft

#endif // BRAFT_V2_LOG_MANAGER_H
