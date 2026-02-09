// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/v2/fsm_caller.h"
#include "braft/v2/log_manager.h"

#include <iostream>
#include <thread>
#include <chrono>

namespace braft {
namespace v2 {

FSMCaller::FSMCaller()
    : _last_applied_index(0),
      _last_applied_term(0),
      _commit_index(0),
      _running(false),
      _stop_requested(false) {
}

FSMCaller::~FSMCaller() {
    shutdown();
}

bool FSMCaller::init(const FSMCallerOptions& options) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_running.load()) {
        return false;
    }

    if (!options.fsm || !options.log_manager) {
        std::cerr << "FSM and LogManager must be provided" << std::endl;
        return false;
    }

    _options = options;
    _running.store(true);
    _stop_requested.store(false);

    // Start apply thread
    _thread.reset(new std::thread([this]() {
        this->run();
    }));

    std::cout << "FSMCaller: initialized with max_batch="
              << _options.max_apply_batch << std::endl;

    return true;
}

void FSMCaller::shutdown() {
    if (!_running.load()) {
        return;
    }

    _running.store(false);
    _stop_requested.store(true);
    _cv.notify_all();

    if (_thread && _thread->joinable()) {
        _thread->join();
    }

    std::lock_guard<std::mutex> lock(_mutex);

    // Clear queue
    while (!_apply_queue.empty()) {
        _apply_queue.pop();
    }

    _thread.reset();
}

void FSMCaller::join() {
    if (_thread && _thread->joinable()) {
        _thread->join();
    }
}

void FSMCaller::onCommitted(int64_t commit_index) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (commit_index <= _commit_index) {
        return;  // Already committed
    }

    _commit_index = commit_index;
    _cv.notify_one();

    std::cout << "FSMCaller: commit_index updated to " << commit_index
              << ", last_applied=" << _last_applied_index << std::endl;
}

void FSMCaller::onSnapshotInstalled(int64_t last_included_index,
                                     int64_t last_included_term) {
    std::lock_guard<std::mutex> lock(_mutex);

    // Update last applied if snapshot is newer
    if (last_included_index > _last_applied_index) {
        _last_applied_index = last_included_index;
        _last_applied_term = last_included_term;

        std::cout << "FSMCaller: snapshot applied, index=" << last_included_index
                  << ", term=" << last_included_term << std::endl;
    }
}

bool FSMCaller::triggerSnapshot(int64_t last_included_index) {
    if (!_options.fsm) {
        return false;
    }

    // Get the term of the last included index
    int64_t last_included_term = 0;
    if (last_included_index > 0) {
        auto entry = _options.log_manager->getEntry(last_included_index);
        if (entry) {
            last_included_term = entry->term;
        } else {
            std::cerr << "FSMCaller: failed to get entry at index "
                      << last_included_index << " for snapshot" << std::endl;
            return false;
        }
    }

    try {
        std::cout << "FSMCaller: triggering snapshot save at index="
                  << last_included_index << " term=" << last_included_term << std::endl;

        return _options.fsm->saveSnapshot(last_included_index, last_included_term);
    } catch (const std::exception& e) {
        std::cerr << "FSMCaller: exception while saving snapshot: "
                  << e.what() << std::endl;
        return false;
    }
}

void FSMCaller::run() {
    while (_running.load() && !_stop_requested.load()) {
        std::unique_lock<std::mutex> lock(_mutex);

        // Wait for new commits
        _cv.wait(lock, [this]() {
            return _commit_index > _last_applied_index ||
                   !_running.load() ||
                   _stop_requested.load();
        });

        if (!_running.load() || _stop_requested.load()) {
            break;
        }

        // Apply a batch of logs
        applyBatch();

        // Sleep briefly to avoid busy waiting
        lock.unlock();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(_options.apply_interval_ms));
    }

    std::cout << "FSMCaller: thread exiting" << std::endl;
}

void FSMCaller::applyBatch() {
    int batch_count = 0;

    // Apply logs from last_applied + 1 to commit_index
    while (_last_applied_index < _commit_index &&
           batch_count < _options.max_apply_batch) {

        int64_t next_index = _last_applied_index + 1;

        // Get log entry from LogManager
        auto entry = _options.log_manager->getEntry(next_index);
        if (!entry) {
            std::cerr << "FSMCaller: failed to get entry at index "
                      << next_index << std::endl;
            break;
        }

        // Create apply task
        ApplyTask task(entry->index, entry->term, entry->type, entry->data);

        // Unlock during apply to allow other operations
        _mutex.unlock();
        bool success = applyLog(task);
        _mutex.lock();

        if (success) {
            _last_applied_index = entry->index;
            _last_applied_term = entry->term;
            batch_count++;
        } else {
            std::cerr << "FSMCaller: failed to apply log at index "
                      << entry->index << std::endl;
            break;
        }
    }

    if (batch_count > 0) {
        std::cout << "FSMCaller: applied " << batch_count
                  << " logs, last_applied=" << _last_applied_index << std::endl;
    }
}

bool FSMCaller::applyLog(const ApplyTask& task) {
    if (!_options.fsm) {
        return false;
    }

    try {
        return _options.fsm->apply(task.index, task.term, task.type, task.data);
    } catch (const std::exception& e) {
        std::cerr << "FSMCaller: exception while applying log: "
                  << e.what() << std::endl;
        return false;
    }
}

} // namespace v2
} // namespace braft
