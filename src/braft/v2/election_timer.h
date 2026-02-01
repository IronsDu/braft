// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// ElectionTimer v2 - Triggers election when no heartbeat received

#ifndef BRAFT_V2_ELECTION_TIMER_H
#define BRAFT_V2_ELECTION_TIMER_H

#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>
#include <condition_variable>
#include <random>

namespace braft {
namespace v2 {

class Node;

/**
 * ElectionTimerOptions - Configuration for ElectionTimer
 */
struct ElectionTimerOptions {
    int election_timeout_ms;     // Base election timeout
    int timeout_variation_ms;    // Random variation to avoid split vote
    int heartbeat_timeout_ms;    // Heartbeat timeout (shorter)

    ElectionTimerOptions()
        : election_timeout_ms(1000),
          timeout_variation_ms(500),
          heartbeat_timeout_ms(100) {}
};

/**
 * ElectionCallback - Function called when election timeout occurs
 */
using ElectionCallback = std::function<void()>;

/**
 * ElectionTimer - Triggers election when leader fails
 *
 * This is a simplified election timer that:
 * - Runs in a separate thread
 * - Resets on heartbeat received
 * - Triggers election callback on timeout
 * - Adds random jitter to prevent simultaneous elections
 *
 * TODO: In production, this should use:
 * - folly::coro for async operations
 * - High-resolution timers
 * - Adaptive timeout based on network conditions
 */
class ElectionTimer {
public:
    ElectionTimer();
    ~ElectionTimer();

    /**
     * Initialize the timer
     * @param options Configuration options
     * @param callback Function to call on timeout
     * @return true on success
     */
    bool init(const ElectionTimerOptions& options,
              ElectionCallback callback);

    /**
     * Start the timer
     */
    void start();

    /**
     * Stop the timer
     */
    void stop();

    /**
     * Reset the timer (called when heartbeat received)
     */
    void reset();

    /**
     * Wait for timer thread to finish
     */
    void join();

    // ========== Accessors ==========

    /**
     * Check if running
     */
    bool isRunning() const { return _running; }

    /**
     * Get current timeout value (with random jitter)
     */
    int getCurrentTimeout() const { return _current_timeout_ms; }

private:
    /**
     * Main timer loop
     */
    void run();

    /**
     * Generate random timeout with jitter
     */
    int generateTimeout();

    /**
     * Sleep until next check
     */
    bool sleepUntilTimeout();

private:
    ElectionTimerOptions _options;
    ElectionCallback _callback;

    // Timer state
    int _current_timeout_ms;
    std::atomic<int64_t> _last_reset_time;
    std::atomic<bool> _running;
    std::atomic<bool> _stop_requested;

    // Random number generator for jitter
    std::mt19937 _rng;
    std::uniform_int_distribution<int> _dist;

    // Thread management
    std::unique_ptr<std::thread> _thread;

    // Synchronization
    mutable std::mutex _mutex;
    std::condition_variable _cv;
};

} // namespace v2
} // namespace braft

#endif // BRAFT_V2_ELECTION_TIMER_H
