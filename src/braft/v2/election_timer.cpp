// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/v2/election_timer.h"

#include <iostream>
#include <chrono>
#include <random>

namespace braft {
namespace v2 {

ElectionTimer::ElectionTimer()
    : _current_timeout_ms(0),
      _last_reset_time(0),
      _running(false),
      _stop_requested(false),
      _rng(std::random_device{}()),
      _dist(0, 1) {  // Will be set in init
}

ElectionTimer::~ElectionTimer() {
    stop();
}

bool ElectionTimer::init(const ElectionTimerOptions& options,
                         ElectionCallback callback) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_running.load()) {
        return false;
    }

    _options = options;
    _callback = callback;

    // Initialize random distribution for jitter
    _dist = std::uniform_int_distribution<int>(
        0, _options.timeout_variation_ms);

    std::cout << "ElectionTimer: initialized with timeout "
              << _options.election_timeout_ms << "ms +/- "
              << _options.timeout_variation_ms << "ms" << std::endl;

    return true;
}

void ElectionTimer::start() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_running.load()) {
        return;
    }

    _running.store(true);
    _stop_requested.store(false);
    _last_reset_time.store(0);
    _current_timeout_ms = generateTimeout();

    // Start timer thread
    _thread.reset(new std::thread([this]() {
        this->run();
    }));

    std::cout << "ElectionTimer: started with timeout "
              << _current_timeout_ms << "ms" << std::endl;
}

void ElectionTimer::stop() {
    std::cout << "ElectionTimer::stop() called" << std::endl;
    if (!_running.load()) {
        std::cout << "ElectionTimer::stop() - not running, returning" << std::endl;
        return;
    }

    std::cout << "ElectionTimer::stop() - stopping timer" << std::endl;
    _running.store(false);
    _stop_requested.store(true);
    _cv.notify_all();

    if (_thread && _thread->joinable()) {
        _thread->join();
    }

    std::cout << "ElectionTimer: stopped" << std::endl;
}

void ElectionTimer::reset() {
    if (!_running.load()) {
        return;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    _last_reset_time.store(now);
    _current_timeout_ms = generateTimeout();

    // Notify the timer thread to restart
    _cv.notify_one();

    std::cout << "ElectionTimer: reset, new timeout "
              << _current_timeout_ms << "ms" << std::endl;
}

void ElectionTimer::join() {
    if (_thread && _thread->joinable()) {
        _thread->join();
    }
}

void ElectionTimer::run() {
    while (_running.load() && !_stop_requested.load()) {
        if (!sleepUntilTimeout()) {
            break;
        }

        // Check if we should stop
        if (!_running.load() || _stop_requested.load()) {
            break;
        }

        // Election timeout! Trigger callback
        std::cout << "ElectionTimer: TIMEOUT! triggering election..." << std::endl;

        if (_callback) {
            try {
                _callback();
            } catch (const std::exception& e) {
                std::cerr << "ElectionTimer: exception in callback: "
                          << e.what() << std::endl;
            }
        }

        // Reset timer after triggering
        _current_timeout_ms = generateTimeout();
        _last_reset_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::cout << "ElectionTimer: thread exiting" << std::endl;
}

int ElectionTimer::generateTimeout() {
    // Generate random timeout: base + random jitter
    int jitter = _dist(_rng);
    return _options.election_timeout_ms + jitter;
}

bool ElectionTimer::sleepUntilTimeout() {
    std::unique_lock<std::mutex> lock(_mutex);

    auto last_reset = _last_reset_time.load();

    std::cout << "ElectionTimer: waiting for " << _current_timeout_ms << "ms" << std::endl;

    // Wait for timeout or reset
    bool timed_out = _cv.wait_for(lock, std::chrono::milliseconds(_current_timeout_ms),
        [this, last_reset]() {
            return _stop_requested.load() ||
                   _last_reset_time.load() != last_reset;
        });

    std::cout << "ElectionTimer: wait finished, timed_out=" << timed_out
              << ", _stop_requested=" << _stop_requested.load()
              << ", _running=" << _running.load() << std::endl;

    // If wait returned because of reset, return true to continue loop
    // If wait timed out, trigger election
    return !timed_out || _last_reset_time.load() != last_reset;
}

} // namespace v2
} // namespace braft
