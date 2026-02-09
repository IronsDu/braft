// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/v2/log_manager.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace braft {
namespace v2 {

namespace {
    // Create directory if it doesn't exist
    bool ensureDirExists(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            return S_ISDIR(st.st_mode);
        }
        // Try to create directory
        return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
    }
}

LogManager::LogManager()
    : _first_log_index(0),
      _last_log_index(0),
      _snapshot_index(0),
      _snapshot_term(0),
      _running(false) {
}

LogManager::~LogManager() {
    shutdown();
}

bool LogManager::init(const LogManagerOptions& options) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_running.load()) {
        return false;
    }

    _options = options;

    // Create data directory if persistence is enabled
    if (_options.enable_persistence && !_options.data_dir.empty()) {
        if (!ensureDirExists(_options.data_dir)) {
            std::cerr << "Failed to create data directory: "
                      << _options.data_dir << std::endl;
            return false;
        }

        // Load from disk
        if (!loadFromDisk()) {
            std::cout << "No existing logs found, starting fresh" << std::endl;
        }
    }

    _running.store(true);
    return true;
}

void LogManager::shutdown() {
    if (!_running.load()) {
        return;
    }

    _running.store(false);

    std::lock_guard<std::mutex> lock(_mutex);

    // TODO: Persist remaining logs to disk
    _logs.clear();
    _first_log_index = 0;
    _last_log_index = 0;
}

int64_t LogManager::appendEntry(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_running.load()) {
        return -1;
    }

    // Create new entry with auto-increment index
    LogEntry new_entry = entry;
    if (_last_log_index == 0) {
        new_entry.index = 1;
        _first_log_index = 1;
    } else {
        new_entry.index = _last_log_index + 1;
    }

    _logs.push_back(new_entry);
    _last_log_index = new_entry.index;

    // Persist to disk if enabled
    if (_options.enable_persistence) {
        persistEntry(new_entry);
    }

    return new_entry.index;
}

int64_t LogManager::appendEntries(const std::vector<LogEntry>& entries) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_running.load() || entries.empty()) {
        return -1;
    }

    for (const auto& entry : entries) {
        LogEntry new_entry = entry;
        if (_last_log_index == 0) {
            new_entry.index = 1;
            _first_log_index = 1;
        } else {
            new_entry.index = _last_log_index + 1;
        }

        _logs.push_back(new_entry);
        _last_log_index = new_entry.index;

        // Persist to disk if enabled
        if (_options.enable_persistence) {
            persistEntry(new_entry);
        }
    }

    return _last_log_index;
}

std::shared_ptr<LogEntry> LogManager::getEntry(int64_t index) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!isValidIndex(index)) {
        return nullptr;
    }

    // Calculate position in deque
    size_t pos = index - _first_log_index;
    if (pos >= _logs.size()) {
        return nullptr;
    }

    // Return a copy (since we're storing by value, not pointer)
    return std::make_shared<LogEntry>(_logs[pos]);
}

int64_t LogManager::getTerm(int64_t index) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!isValidIndex(index)) {
        return 0;
    }

    // Check snapshot
    if (index == _snapshot_index) {
        return _snapshot_term;
    }

    // Calculate position in deque
    size_t pos = index - _first_log_index;
    if (pos >= _logs.size()) {
        return 0;
    }

    return _logs[pos].term;
}

int64_t LogManager::firstLogIndex() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_mutex));
    return _first_log_index;
}

int64_t LogManager::lastLogIndex() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_mutex));
    return _last_log_index;
}

int64_t LogManager::lastLogTerm() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_mutex));

    if (_logs.empty()) {
        return _snapshot_term;
    }

    return _logs.back().term;
}

std::pair<int64_t, int64_t> LogManager::lastLogId() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_mutex));
    return std::make_pair(_last_log_index, lastLogTerm());
}

bool LogManager::truncatePrefix(int64_t first_index_kept) {
    std::lock_guard<std::mutex> lock(_mutex);
    truncatePrefixInternal(first_index_kept);
    return true;
}

void LogManager::truncatePrefixInternal(int64_t first_index_kept) {
    // Must be called with lock held
    if (!isValidIndex(first_index_kept) || first_index_kept < _first_log_index) {
        return;
    }

    // Calculate how many to remove
    size_t remove_count = first_index_kept - _first_log_index;
    for (size_t i = 0; i < remove_count && !_logs.empty(); ++i) {
        _logs.pop_front();
    }

    _first_log_index = first_index_kept;

    std::cout << "LogManager: truncated prefix up to index " << first_index_kept
              << ", new first_index=" << _first_log_index << std::endl;
}

bool LogManager::truncateSuffix(int64_t last_index_kept) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!isValidIndex(last_index_kept) || last_index_kept > _last_log_index) {
        return false;
    }

    // Calculate new size
    size_t new_size = last_index_kept - _first_log_index + 1;
    while (_logs.size() > new_size) {
        _logs.pop_back();
    }

    _last_log_index = last_index_kept;

    std::cout << "LogManager: truncated suffix from index " << last_index_kept
              << ", new last_index=" << _last_log_index << std::endl;

    return true;
}

void LogManager::setSnapshot(int64_t last_included_index, int64_t last_included_term) {
    std::lock_guard<std::mutex> lock(_mutex);

    _snapshot_index = last_included_index;
    _snapshot_term = last_included_term;

    // Truncate logs that are included in snapshot
    if (_first_log_index <= last_included_index) {
        truncatePrefixInternal(last_included_index + 1);
    }

    std::cout << "LogManager: set snapshot index=" << last_included_index
              << ", term=" << last_included_term << std::endl;
}

std::pair<int64_t, int64_t> LogManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_mutex));
    return std::make_pair(_snapshot_index, _snapshot_term);
}

size_t LogManager::getLogCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_mutex));
    return _logs.size();
}

bool LogManager::isEmpty() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_mutex));
    return _logs.empty() && _snapshot_index == 0;
}

void LogManager::persistEntry(const LogEntry& entry) {
    if (_options.data_dir.empty()) {
        return;
    }

    std::string filepath = _options.data_dir + "/log_entries.dat";

    // Open in binary append mode
    std::ofstream outfile(filepath, std::ios::app | std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open log file: " << filepath << std::endl;
        return;
    }

    // Write binary format: index(8) + term(8) + type(4) + data_size(4) + data
    outfile.write(reinterpret_cast<const char*>(&entry.index), sizeof(entry.index));
    outfile.write(reinterpret_cast<const char*>(&entry.term), sizeof(entry.term));
    outfile.write(reinterpret_cast<const char*>(&entry.type), sizeof(entry.type));

    int64_t data_size = static_cast<int64_t>(entry.data.size());
    outfile.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));

    if (!entry.data.empty()) {
        outfile.write(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
    }

    outfile.flush();

    // Ensure data is written to disk
    int fd = open(filepath.c_str(), O_WRONLY | O_APPEND);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }

    outfile.close();
}

bool LogManager::loadFromDisk() {
    if (_options.data_dir.empty()) {
        return false;
    }

    std::string filepath = _options.data_dir + "/log_entries.dat";

    // Open in binary read mode
    std::ifstream infile(filepath, std::ios::binary);
    if (!infile.is_open()) {
        // File doesn't exist yet, that's OK
        return true;
    }

    size_t loaded_count = 0;
    while (infile.good() && !infile.eof()) {
        LogEntry entry;

        // Read index
        if (!infile.read(reinterpret_cast<char*>(&entry.index), sizeof(entry.index))) {
            break;  // EOF or error
        }

        // Read term
        if (!infile.read(reinterpret_cast<char*>(&entry.term), sizeof(entry.term))) {
            break;
        }

        // Read type
        if (!infile.read(reinterpret_cast<char*>(&entry.type), sizeof(entry.type))) {
            break;
        }

        // Read data size
        int64_t data_size = 0;
        if (!infile.read(reinterpret_cast<char*>(&data_size), sizeof(data_size))) {
            break;
        }

        // Read data
        if (data_size > 0) {
            entry.data.resize(data_size);
            if (!infile.read(reinterpret_cast<char*>(entry.data.data()), data_size)) {
                std::cerr << "Failed to read log data at index " << entry.index << std::endl;
                break;
            }
        }

        _logs.push_back(entry);

        if (_first_log_index == 0 || entry.index < _first_log_index) {
            _first_log_index = entry.index;
        }
        _last_log_index = entry.index;

        loaded_count++;
    }

    infile.close();

    std::cout << "Loaded " << loaded_count << " log entries from disk" << std::endl;
    return true;
}

bool LogManager::isValidIndex(int64_t index) const {
    // Check if within range
    if (index < _first_log_index || index > _last_log_index) {
        // Check snapshot
        if (index == _snapshot_index && index < _first_log_index) {
            return true;
        }
        return false;
    }
    return true;
}

} // namespace v2
} // namespace braft
