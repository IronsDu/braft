// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// FSMCaller v2 - Apply committed logs to state machine

#ifndef BRAFT_V2_FSM_CALLER_H
#define BRAFT_V2_FSM_CALLER_H

#include <memory>
#include <mutex>
#include <atomic>
#include <queue>
#include <functional>
#include <thread>
#include <condition_variable>

#include "raft_types.h"

namespace braft {
namespace v2 {

// Forward declarations
class LogManager;
class Node;

/**
 * FSM - Finite State Machine interface
 *
 * Users of braft implement this interface to receive committed logs
 */
class FSM {
public:
    virtual ~FSM() = default;

    /**
     * Apply a committed log entry to the state machine
     * @param index Log index
     * @param term Log term
     * @param type Entry type (ENTRY_TYPE_*)
     * @param data Log data
     * @return true on success
     */
    virtual bool apply(int64_t index, int64_t term, int32_t type,
                      const std::vector<uint8_t>& data) = 0;

    /**
     * Apply a snapshot to the state machine
     * @param last_included_index Last index included in snapshot
     * @param last_included_term Term of last included index
     * @return true on success
     */
    virtual bool applySnapshot(int64_t last_included_index,
                              int64_t last_included_term) = 0;

    /**
     * Save a snapshot of the current state machine
     * @param last_included_index Last index to include in snapshot
     * @param last_included_term Term of last included index
     * @return true on success
     */
    virtual bool saveSnapshot(int64_t last_included_index,
                             int64_t last_included_term) = 0;
};

/**
 * FSMCallerOptions - Configuration for FSMCaller
 */
struct FSMCallerOptions {
    FSM* fsm;                      // User's state machine (not owned)
    LogManager* log_manager;       // Log manager (not owned)
    int max_apply_batch;           // Maximum logs to apply in one batch
    int apply_interval_ms;         // Interval between applies

    FSMCallerOptions()
        : fsm(nullptr),
          log_manager(nullptr),
          max_apply_batch(100),
          apply_interval_ms(10) {}
};

/**
 * Task for applying logs to FSM
 */
struct ApplyTask {
    int64_t index;
    int64_t term;
    int32_t type;  // Entry type (ENTRY_TYPE_*)
    std::vector<uint8_t> data;

    ApplyTask() : index(0), term(0), type(0) {}
    ApplyTask(int64_t i, int64_t t, int32_t ty, const std::vector<uint8_t>& d)
        : index(i), term(t), type(ty), data(d) {}
};

/**
 * FSMCaller - Applies committed logs to state machine
 *
 * This is a simplified version of FSMCaller that:
 * - Runs in a separate thread
 * - Applies logs in batches
 * - Calls user's FSM::apply() for each log
 * - Tracks last applied index
 *
 * TODO: In production, this should use:
 * - folly::coro for async operations
 * - Proper error handling and retry
 * - Snapshot installation coordination
 */
class FSMCaller {
public:
    FSMCaller();
    ~FSMCaller();

    /**
     * Initialize the FSM caller
     * @param options Configuration options
     * @return true on success
     */
    bool init(const FSMCallerOptions& options);

    /**
     * Shutdown the FSM caller
     */
    void shutdown();

    /**
     * Wait for FSM caller to finish
     */
    void join();

    // ========== Operations ==========

    /**
     * Notify that logs up to |commit_index| are committed
     * @param commit_index Highest committed index
     */
    void onCommitted(int64_t commit_index);

    /**
     * Notify that a snapshot is being installed
     * @param last_included_index Last index in snapshot
     * @param last_included_term Term of last included index
     */
    void onSnapshotInstalled(int64_t last_included_index,
                             int64_t last_included_term);

    /**
     * Trigger snapshot creation
     * @param last_included_index Last index to include in snapshot
     * @return true if snapshot creation was triggered
     */
    bool triggerSnapshot(int64_t last_included_index);

    // ========== Accessors ==========

    /**
     * Get last applied index
     */
    int64_t getLastAppliedIndex() const { return _last_applied_index; }

    /**
     * Get last applied term
     */
    int64_t getLastAppliedTerm() const { return _last_applied_term; }

    /**
     * Check if running
     */
    bool isRunning() const { return _running; }

private:
    /**
     * Main loop for applying logs
     */
    void run();

    /**
     * Apply a batch of logs to FSM
     */
    void applyBatch();

    /**
     * Apply a single log entry
     */
    bool applyLog(const ApplyTask& task);

private:
    FSMCallerOptions _options;

    // Apply state
    int64_t _last_applied_index;
    int64_t _last_applied_term;
    int64_t _commit_index;

    // Task queue
    std::queue<ApplyTask> _apply_queue;

    // Thread management
    std::unique_ptr<std::thread> _thread;
    std::atomic<bool> _running;
    std::atomic<bool> _stop_requested;

    // Synchronization
    mutable std::mutex _mutex;
    std::condition_variable _cv;
};

} // namespace v2
} // namespace braft

#endif // BRAFT_V2_FSM_CALLER_H
