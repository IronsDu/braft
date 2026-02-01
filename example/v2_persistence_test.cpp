// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/v2/node.h"
#include "braft/v2/log_manager.h"
#include "braft/v2/fsm_caller.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>

using namespace braft::v2;

// Simple FSM for testing
class TestFSM : public FSM {
public:
    TestFSM() : _apply_count(0), _last_index(0) {}

    bool apply(int64_t index, int64_t term, int32_t type,
               const std::vector<uint8_t>& data) override {
        _apply_count++;
        _last_index = index;
        _logs.push_back({index, term, type, std::string(data.begin(), data.end())});

        std::cout << "[FSM] Applied log: index=" << index << ", term=" << term
                  << ", type=" << type << ", data=" << _logs.back().data << std::endl;
        return true;
    }

    bool saveSnapshot(int64_t last_included_index, int64_t last_included_term) override {
        std::cout << "[FSM] Saving snapshot: index=" << last_included_index
                  << ", term=" << last_included_term << std::endl;
        _snapshot_index = last_included_index;
        _snapshot_term = last_included_term;
        return true;
    }

    int getApplyCount() const { return _apply_count; }
    int64_t getLastIndex() const { return _last_index; }

    void printState() const {
        std::cout << "FSM State:" << std::endl;
        std::cout << "  Apply count: " << _apply_count << std::endl;
        std::cout << "  Last index: " << _last_index << std::endl;
        std::cout << "  Snapshot: index=" << _snapshot_index
                  << ", term=" << _snapshot_term << std::endl;
        std::cout << "  Applied logs:" << std::endl;
        for (const auto& log : _logs) {
            std::cout << "    [" << log.index << "] term=" << log.term
                      << " type=" << log.type << " data=" << log.data << std::endl;
        }
    }

private:
    struct LogInfo {
        int64_t index;
        int64_t term;
        int32_t type;
        std::string data;
    };

    int _apply_count;
    int64_t _last_index;
    int64_t _snapshot_index = 0;
    int64_t _snapshot_term = 0;
    std::vector<LogInfo> _logs;
};

void testBasicPersistence() {
    std::cout << "\n=== Test 1: Basic Persistence ===" << std::endl;

    // Create log manager with persistence enabled
    LogManager log_manager;
    LogManagerOptions options;
    options.enable_persistence = true;
    options.data_dir = "/tmp/braft_persistence_test";

    // Clean up any existing data
    system("rm -rf /tmp/braft_persistence_test");

    if (!log_manager.init(options)) {
        std::cerr << "Failed to init LogManager" << std::endl;
        return;
    }

    std::cout << "LogManager initialized with data_dir: " << options.data_dir << std::endl;

    // Append some entries
    std::vector<std::string> test_data = {
        "hello world",
        "raft consensus",
        "persistence test"
    };

    for (size_t i = 0; i < test_data.size(); ++i) {
        LogEntry entry;
        entry.term = 1;
        entry.type = 1;  // ENTRY_TYPE_DATA
        entry.data.assign(test_data[i].begin(), test_data[i].end());

        int64_t index = log_manager.appendEntry(entry);
        std::cout << "Appended entry at index " << index << ": " << test_data[i] << std::endl;
    }

    std::cout << "First run: appended " << log_manager.getLogCount() << " entries" << std::endl;
    std::cout << "First index: " << log_manager.firstLogIndex() << std::endl;
    std::cout << "Last index: " << log_manager.lastLogIndex() << std::endl;

    log_manager.shutdown();
    std::cout << "LogManager shut down" << std::endl;
}

void testRecovery() {
    std::cout << "\n=== Test 2: Recovery from Disk ===" << std::endl;

    // Create new log manager with same data directory
    LogManager log_manager;
    LogManagerOptions options;
    options.enable_persistence = true;
    options.data_dir = "/tmp/braft_persistence_test";

    if (!log_manager.init(options)) {
        std::cerr << "Failed to init LogManager" << std::endl;
        return;
    }

    std::cout << "LogManager re-initialized" << std::endl;
    std::cout << "Log count after recovery: " << log_manager.getLogCount() << std::endl;
    std::cout << "First index: " << log_manager.firstLogIndex() << std::endl;
    std::cout << "Last index: " << log_manager.lastLogIndex() << std::endl;

    // Verify recovered entries
    bool all_correct = true;
    for (int64_t i = log_manager.firstLogIndex(); i <= log_manager.lastLogIndex(); ++i) {
        auto entry = log_manager.getEntry(i);
        if (entry) {
            std::string data(entry->data.begin(), entry->data.end());
            std::cout << "Entry " << i << ": term=" << entry->term
                      << ", type=" << entry->type << ", data=" << data << std::endl;
        } else {
            std::cerr << "Failed to get entry at index " << i << std::endl;
            all_correct = false;
        }
    }

    if (all_correct && log_manager.getLogCount() == 3) {
        std::cout << "✓ Recovery test PASSED" << std::endl;
    } else {
        std::cerr << "✗ Recovery test FAILED" << std::endl;
    }

    log_manager.shutdown();
}

void testAppendAfterRecovery() {
    std::cout << "\n=== Test 3: Append After Recovery ===" << std::endl;

    LogManager log_manager;
    LogManagerOptions options;
    options.enable_persistence = true;
    options.data_dir = "/tmp/braft_persistence_test";

    if (!log_manager.init(options)) {
        std::cerr << "Failed to init LogManager" << std::endl;
        return;
    }

    int64_t last_index_before = log_manager.lastLogIndex();
    std::cout << "Last index before append: " << last_index_before << std::endl;

    // Append new entries
    LogEntry entry;
    entry.term = 2;
    entry.type = 1;
    std::string new_data = "new entry after recovery";
    entry.data.assign(new_data.begin(), new_data.end());

    int64_t new_index = log_manager.appendEntry(entry);
    std::cout << "Appended new entry at index " << new_index << ": " << new_data << std::endl;

    if (new_index == last_index_before + 1) {
        std::cout << "✓ Index correctly incremented" << std::endl;
    } else {
        std::cerr << "✗ Index mismatch: expected " << (last_index_before + 1)
                  << ", got " << new_index << std::endl;
    }

    log_manager.shutdown();

    // Verify by opening again
    LogManager log_manager2;
    if (!log_manager2.init(options)) {
        std::cerr << "Failed to init LogManager" << std::endl;
        return;
    }

    auto recovered_entry = log_manager2.getEntry(new_index);
    if (recovered_entry) {
        std::string data(recovered_entry->data.begin(), recovered_entry->data.end());
        if (data == new_data && recovered_entry->term == 2) {
            std::cout << "✓ New entry persisted correctly" << std::endl;
            std::cout << "✓ Append after recovery test PASSED" << std::endl;
        } else {
            std::cerr << "✗ New entry data mismatch" << std::endl;
        }
    } else {
        std::cerr << "✗ Failed to retrieve new entry" << std::endl;
    }

    log_manager2.shutdown();
}

void testSnapshotWithPersistence() {
    std::cout << "\n=== Test 4: Snapshot with Persistence ===" << std::endl;

    LogManager log_manager;
    LogManagerOptions options;
    options.enable_persistence = true;
    options.data_dir = "/tmp/braft_persistence_test";

    if (!log_manager.init(options)) {
        std::cerr << "Failed to init LogManager" << std::endl;
        return;
    }

    std::cout << "Log count: " << log_manager.getLogCount() << std::endl;
    std::cout << "Last index: " << log_manager.lastLogIndex() << std::endl;

    // Create snapshot
    int64_t snapshot_index = 2;
    int64_t snapshot_term = log_manager.getTerm(snapshot_index);

    log_manager.setSnapshot(snapshot_index, snapshot_term);
    std::cout << "Created snapshot at index " << snapshot_index << ", term " << snapshot_term << std::endl;

    std::cout << "After snapshot:" << std::endl;
    std::cout << "  First index: " << log_manager.firstLogIndex() << std::endl;
    std::cout << "  Last index: " << log_manager.lastLogIndex() << std::endl;
    std::cout << "  Log count: " << log_manager.getLogCount() << std::endl;

    auto snapshot_info = log_manager.getSnapshot();
    if (snapshot_info.first == snapshot_index && snapshot_info.second == snapshot_term) {
        std::cout << "✓ Snapshot info correct" << std::endl;
        std::cout << "✓ Snapshot test PASSED" << std::endl;
    } else {
        std::cerr << "✗ Snapshot info mismatch" << std::endl;
    }

    log_manager.shutdown();
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "Braft v2 Persistence Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // Test 1: Basic persistence
    testBasicPersistence();

    // Wait a bit for fsync
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test 2: Recovery
    testRecovery();

    // Test 3: Append after recovery
    testAppendAfterRecovery();

    // Test 4: Snapshot with persistence
    testSnapshotWithPersistence();

    // Clean up
    std::cout << "\n=== Cleanup ===" << std::endl;
    system("rm -rf /tmp/braft_persistence_test");
    std::cout << "Test data directory cleaned up" << std::endl;

    std::cout << "\n========================================" << std::endl;
    std::cout << "All persistence tests completed!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
