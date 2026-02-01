// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/v2/node.h"

#include <iostream>
#include <sstream>
#include <memory>
#include <algorithm>

namespace braft {
namespace v2 {

// Simple FSM implementation for demonstration
class DemoFSM : public FSM {
public:
    DemoFSM(Node* node) : _node(node) {}

    bool apply(int64_t index, int64_t term, int32_t type,
              const std::vector<uint8_t>& data) override {
        if (type == 3) {  // ENTRY_TYPE_CONFIGURATION
            // Parse configuration data (newline-separated peer list)
            std::vector<std::string> new_peers;
            std::string peer;
            for (uint8_t c : data) {
                if (c == '\n') {
                    if (!peer.empty()) {
                        new_peers.push_back(peer);
                        peer.clear();
                    }
                } else {
                    peer += static_cast<char>(c);
                }
            }
            if (!peer.empty()) {
                new_peers.push_back(peer);
            }

            std::cout << "DemoFSM: applying configuration at index=" << index
                      << " with " << new_peers.size() << " peers" << std::endl;

            // Apply configuration to node
            if (_node) {
                _node->applyConfiguration(new_peers);
            }
        } else {
            std::cout << "DemoFSM: applied log index=" << index
                      << " term=" << term
                      << " type=" << type
                      << " data_size=" << data.size() << std::endl;
        }
        return true;
    }

    bool applySnapshot(int64_t last_included_index,
                      int64_t last_included_term) override {
        std::cout << "DemoFSM: applied snapshot index=" << last_included_index
                  << " term=" << last_included_term << std::endl;
        return true;
    }

    bool saveSnapshot(int64_t last_included_index,
                     int64_t last_included_term) override {
        std::cout << "DemoFSM: saved snapshot index=" << last_included_index
                  << " term=" << last_included_term << std::endl;
        // In production, this would:
        // 1. Serialize state machine state to disk
        // 2. Write snapshot metadata
        // 3. Optionally compress the snapshot
        return true;
    }

private:
    Node* _node;  // Not owned, for configuration changes
};

Node::Node(const std::string& group_id,
           const std::string& server_id,
           const std::vector<std::string>& peers)
    : _group_id(group_id),
      _server_id(server_id),
      _peers(peers),
      _current_term(0),
      _voted_for(""),
      _leader_id(""),
      _state("FOLLOWER"),
      _last_log_index(0),
      _last_log_term(0),
      _commit_index(0),
      _votes_granted(0),
      _votes_refused(0),
      _election_complete(false),
      _running(false) {

    // Create core modules
    _log_manager.reset(new LogManager());
    _fsm_caller.reset(new FSMCaller());
    _election_timer.reset(new ElectionTimer());
}

Node::~Node() {
    shutdown();
}

bool Node::start(int port) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_running.load()) {
        return false;
    }

    try {
        // Initialize and start LogManager
        LogManagerOptions log_options;
        log_options.enable_persistence = false;  // Disable for demo
        if (!_log_manager->init(log_options)) {
            std::cerr << "Failed to initialize LogManager" << std::endl;
            return false;
        }

        // Initialize and start FSMCaller
        FSMCallerOptions fsm_options;
        fsm_options.log_manager = _log_manager.get();
        fsm_options.fsm = new DemoFSM(this);  // Demo FSM with node pointer
        fsm_options.max_apply_batch = 100;
        fsm_options.apply_interval_ms = 10;
        if (!_fsm_caller->init(fsm_options)) {
            std::cerr << "Failed to initialize FSMCaller" << std::endl;
            return false;
        }

        // Initialize ElectionTimer
        ElectionTimerOptions timer_options;
        timer_options.election_timeout_ms = 3000;  // 3 second base timeout (increased for multi-node)
        timer_options.timeout_variation_ms = 1000;  // +/- 1000ms jitter
        timer_options.heartbeat_timeout_ms = 100;
        _election_timer->init(timer_options,
            [this]() { this->onElectionTimeout(); });
        _election_timer->start();

        // Create RPC service
        _raft_service = std::make_shared<RaftRpcService>(this);

        // Create and start Thrift server
        _server.reset(new ThriftServer());
        if (!_server->start(port, _raft_service)) {
            _election_timer->stop();
            _fsm_caller->shutdown();
            _log_manager->shutdown();
            _raft_service.reset();
            _server.reset();
            return false;
        }

        _running.store(true);
        _state = "FOLLOWER";

        std::cout << "Node " << _server_id << " started on port " << port
                  << " with LogManager, FSMCaller, ElectionTimer" << std::endl;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Failed to start node: " << e.what() << std::endl;
        _election_timer.reset();
        _fsm_caller.reset();
        _log_manager.reset();
        _raft_service.reset();
        _server.reset();
        return false;
    }
}

void Node::shutdown() {
    if (!_running.load()) {
        return;
    }

    _running.store(false);

    std::lock_guard<std::mutex> lock(_mutex);

    // Stop all replicators first
    stepDown();

    // Stop core modules
    if (_election_timer) {
        _election_timer->stop();
    }
    if (_fsm_caller) {
        _fsm_caller->shutdown();
    }
    if (_log_manager) {
        _log_manager->shutdown();
    }

    if (_server) {
        _server->stop();
    }

    _raft_service.reset();
    _server.reset();
}

void Node::handlePreVote(const RequestVoteRequest& req,
                          RequestVoteResponse& resp) {
    std::lock_guard<std::mutex> lock(_mutex);

    // Pre-vote is a optimization to prevent disrupting the leader
    // Check if we would vote for this candidate
    resp.term = _current_term;

    // Check if candidate's log is at least as up-to-date as ours
    bool log_ok = (req.last_log_term > _last_log_term) ||
                  (req.last_log_term == _last_log_term &&
                   req.last_log_index >= _last_log_index);

    // Grant pre-vote if log is OK (term check is relaxed for pre-vote)
    resp.granted = log_ok;

    std::cout << "Node " << _server_id << " received PreVote from "
              << req.server_id << " term " << req.term
              << " granted=" << resp.granted << std::endl;
}

void Node::handleRequestVote(const RequestVoteRequest& req,
                             RequestVoteResponse& resp) {
    std::lock_guard<std::mutex> lock(_mutex);

    resp.granted = false;

    // If request's term is smaller, reject
    if (req.term < _current_term) {
        resp.term = _current_term;  // Set response term to current term
        std::cout << "Node " << _server_id << " rejected RequestVote from "
                  << req.server_id << " term " << req.term
                  << " (current term " << _current_term << ")" << std::endl;
        return;
    }

    // If request's term is higher, update and step down
    if (req.term > _current_term) {
        _current_term = req.term;
        _voted_for = "";
        if (_state == "LEADER") {
            stepDown();
        }
        _state = "FOLLOWER";
    }

    // Set response term to (possibly updated) current term
    resp.term = _current_term;

    // Check if we already voted for someone else in this term
    if (!_voted_for.empty() && _voted_for != req.server_id) {
        std::cout << "Node " << _server_id << " already voted for "
                  << _voted_for << " in term " << _current_term << std::endl;
        return;
    }

    // Check if candidate's log is at least as up-to-date as ours
    bool log_ok = (req.last_log_term > _last_log_term) ||
                  (req.last_log_term == _last_log_term &&
                   req.last_log_index >= _last_log_index);

    if (log_ok) {
        _voted_for = req.server_id;
        resp.granted = true;
        std::cout << "Node " << _server_id << " voted for "
                  << req.server_id << " in term " << _current_term << std::endl;
    } else {
        std::cout << "Node " << _server_id << " rejected RequestVote from "
                  << req.server_id << " (log not up-to-date)" << std::endl;
    }
}

void Node::handleAppendEntries(const AppendEntriesRequest& req,
                               AppendEntriesResponse& resp) {
    std::lock_guard<std::mutex> lock(_mutex);

    resp.term = _current_term;
    resp.success = false;

    // If request's term is smaller, reject
    if (req.term < _current_term) {
        std::cout << "Node " << _server_id << " rejected AppendEntries from "
                  << req.server_id << " term " << req.term
                  << " (current term " << _current_term << ")" << std::endl;
        return;
    }

    // If request's term is higher, update and step down
    if (req.term > _current_term) {
        _current_term = req.term;
        if (_state == "LEADER") {
            stepDown();
        }
        _state = "FOLLOWER";
    }

    // Update leader_id
    _leader_id = req.server_id;

    // For heartbeat (empty entries), just return success
    if (req.entries.size() == 0) {
        resp.success = true;

        // Get log info from LogManager
        int64_t last_index = _log_manager->lastLogIndex();
        resp.__set_last_log_index(last_index);

        // Reset election timer on heartbeat
        _election_timer->reset();

        // Update commit index if leader's commit is higher
        if (req.committed_index > _commit_index) {
            _commit_index = std::min(req.committed_index, last_index);

            // Notify FSMCaller about new commit index
            _fsm_caller->onCommitted(_commit_index);
        }
        return;
    }

    // Check log consistency
    if (req.prev_log_index > 0) {
        int64_t prev_term = _log_manager->getTerm(req.prev_log_index);
        if (prev_term != req.prev_log_term) {
            // Log inconsistency, reject
            resp.success = false;
            resp.__set_last_log_index(_log_manager->lastLogIndex());
            std::cout << "Node " << _server_id << " log inconsistency at index "
                      << req.prev_log_index << " expected term " << req.prev_log_term
                      << " got " << prev_term << std::endl;
            return;
        }
    }

    // Append entries to LogManager
    for (const auto& entry_meta : req.entries) {
        LogEntry entry;
        entry.index = req.prev_log_index + 1 + (&entry_meta - &req.entries[0]);
        entry.term = req.term;
        entry.type = static_cast<int32_t>(entry_meta.type);
        // Note: EntryMeta only has metadata, data is typically in attachment
        // For this simplified version, we store empty data
        entry.data.clear();

        _log_manager->appendEntry(entry);
    }

    // Update local log state
    _last_log_index = _log_manager->lastLogIndex();
    _last_log_term = _log_manager->lastLogTerm();

    resp.success = true;
    resp.__set_last_log_index(_last_log_index);

    // Update commit index
    if (req.committed_index > _commit_index) {
        _commit_index = std::min(req.committed_index, _last_log_index);
        _fsm_caller->onCommitted(_commit_index);
    }

    std::cout << "Node " << _server_id << " appended " << req.entries.size()
              << " entries, last_index=" << _last_log_index << std::endl;
}

void Node::handleInstallSnapshot(const InstallSnapshotRequest& req,
                                  InstallSnapshotResponse& resp) {
    std::lock_guard<std::mutex> lock(_mutex);

    resp.term = _current_term;
    resp.success = false;

    // If request's term is smaller, reject
    if (req.term < _current_term) {
        std::cout << "Node " << _server_id << " rejected InstallSnapshot from "
                  << req.server_id << " term " << req.term
                  << " (current term " << _current_term << ")" << std::endl;
        return;
    }

    // If request's term is higher, update and step down
    if (req.term > _current_term) {
        _current_term = req.term;
        if (_state == "LEADER") {
            stepDown();
        }
        _state = "FOLLOWER";
    }

    // Update leader_id
    _leader_id = req.server_id;

    int64_t last_included_index = req.meta.last_included_index;
    int64_t last_included_term = req.meta.last_included_term;

    std::cout << "Node " << _server_id << " received InstallSnapshot from "
              << req.server_id << " last_included_index=" << last_included_index
              << " last_included_term=" << last_included_term << std::endl;

    // Step 1: Set snapshot in LogManager
    _log_manager->setSnapshot(last_included_index, last_included_term);

    // Step 2: Truncate logs that are included in snapshot
    if (last_included_index >= _log_manager->firstLogIndex()) {
        _log_manager->truncatePrefix(last_included_index + 1);
    }

    // Step 3: Update log state
    _last_log_index = _log_manager->lastLogIndex();
    _last_log_term = _log_manager->lastLogTerm();

    // Step 4: Update commit index
    if (last_included_index > _commit_index) {
        _commit_index = last_included_index;

        // Step 5: Notify FSMCaller to apply snapshot
        _fsm_caller->onSnapshotInstalled(last_included_index, last_included_term);

        std::cout << "Node " << _server_id << " installed snapshot "
                  << " (commit_index updated to " << _commit_index << ")" << std::endl;
    }

    resp.success = true;
}

void Node::handleTimeoutNow(const TimeoutNowRequest& req,
                             TimeoutNowResponse& resp) {
    std::lock_guard<std::mutex> lock(_mutex);

    resp.term = _current_term;
    resp.success = false;

    // If request's term is smaller, reject
    if (req.term < _current_term) {
        return;
    }

    // If request's term is higher, update and step down to follower
    // Then immediately start election
    if (req.term > _current_term) {
        _current_term = req.term;
        if (_state == "LEADER") {
            stepDown();
        }
        _state = "CANDIDATE";

        // Start election (simplified - in production would use election timer)
        std::cout << "Node " << _server_id
                  << " received TimeoutNow, starting election" << std::endl;
    }

    resp.success = true;
}

void Node::becomeLeader() {
    std::lock_guard<std::mutex> lock(_mutex);
    becomeLeaderInternal();
}

void Node::becomeLeaderInternal() {
    // Must be called with mutex held

    // Stop all existing replicators (without additional locking)
    for (auto& replicator : _replicators) {
        if (replicator && replicator->isRunning()) {
            replicator->stop();
            replicator->join();
        }
    }
    _replicators.clear();

    _state = "LEADER";

    // Initialize next_index to last log index + 1 for all followers
    int64_t next_index = _log_manager->lastLogIndex() + 1;

    // Create a replicator for each peer (excluding self)
    for (const auto& peer : _peers) {
        // Skip self
        if (peer == _server_id) {
            continue;
        }

        // Create replicator
        auto replicator = std::make_shared<Replicator>(
            peer, this, _current_term);

        // Initialize next_index and match_index
        replicator->setNextIndex(next_index);
        replicator->setMatchIndex(0);

        // Start replicator
        if (replicator->start()) {
            _replicators.push_back(replicator);
        } else {
            std::cerr << "Failed to start replicator for peer: "
                      << peer << std::endl;
        }
    }

    std::cout << "Node " << _server_id << " became leader for term "
              << _current_term << " with " << _replicators.size()
              << " replicators (next_index=" << next_index << ")" << std::endl;
}

void Node::stepDown() {
    std::lock_guard<std::mutex> lock(_mutex);

    // Stop all replicators
    for (auto& replicator : _replicators) {
        if (replicator && replicator->isRunning()) {
            replicator->stop();
            replicator->join();
        }
    }

    _replicators.clear();

    // Update state if we were leader
    if (_state == "LEADER") {
        _state = "FOLLOWER";
        std::cout << "Node " << _server_id << " stepped down from leadership"
                  << std::endl;
    }
}

void Node::onElectionTimeout() {
    std::cout << "Node " << _server_id << " election timeout, starting election..."
              << std::endl;
    startElection();
}

void Node::startElection() {
    std::unique_lock<std::mutex> lock(_mutex);

    // Only start election if we're follower or candidate
    if (_state == "LEADER") {
        return;  // Already leader
    }

    // Increment term
    _current_term++;
    _state = "CANDIDATE";
    _voted_for = _server_id;

    // Reset election state
    _votes_granted = 1;  // Vote for self
    _votes_refused = 0;
    _election_complete.store(false);

    // Update log state from LogManager
    _last_log_index = _log_manager->lastLogIndex();
    _last_log_term = _log_manager->lastLogTerm();

    std::cout << "Node " << _server_id << " starting election for term "
              << _current_term << " (last_log_index=" << _last_log_index
              << ", last_log_term=" << _last_log_term << ")" << std::endl;

    // Send RequestVote to all peers
    // Note: We need to release lock before sending RPC
    lock.unlock();
    sendRequestVote();
    lock.lock();

    // Check if single-node cluster
    if (_peers.size() <= 1) {
        becomeLeader();
    }

    // Reset election timer after starting election
    _election_timer->reset();
}

void Node::sendRequestVote() {
    // Create RequestVote request
    RequestVoteRequest request;
    request.group_id = _group_id;
    request.server_id = _server_id;
    request.peer_id = _server_id;  // For compatibility
    request.term = _current_term;
    request.last_log_index = _last_log_index;
    request.last_log_term = _last_log_term;

    std::cout << "Node " << _server_id << " sendRequestVote: term=" << _current_term
              << ", last_log_index=" << _last_log_index << std::endl;

    // Send to all peers
    for (const auto& peer : _peers) {
        if (peer == _server_id) {
            continue;  // Skip self
        }

        // Create RPC client for this peer
        std::unique_ptr<RaftRpcClient> client(new RaftRpcClient(peer));

        std::cout << "Node " << _server_id << " sending RequestVote to "
                  << peer << " for term " << _current_term << std::endl;

        try {
            if (client->connect()) {
                std::cout << "Node " << _server_id << " RPC connected to " << peer << std::endl;
                RequestVoteResponse response = client->requestVote(request);
                std::cout << "Node " << _server_id << " received response from " << peer
                          << ": granted=" << response.granted << ", term=" << response.term << std::endl;
                handleVoteResponse(peer, response);
            } else {
                std::cerr << "Failed to connect to peer " << peer << std::endl;
                // Connection failure counts as refused
                handleVoteResponse(peer, RequestVoteResponse());
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception sending RequestVote to " << peer
                      << ": " << e.what() << std::endl;
            handleVoteResponse(peer, RequestVoteResponse());
        }
    }
}

void Node::handleVoteResponse(const std::string& peer_id,
                               const RequestVoteResponse& response) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_election_complete.load()) {
        return;  // Election already decided
    }

    // Check term
    if (response.term > _current_term) {
        // Peer has higher term, step down
        _current_term = response.term;
        _state = "FOLLOWER";
        _voted_for = "";
        _election_complete.store(true);
        std::cout << "Node " << _server_id << " discovered higher term "
                  << response.term << " from " << peer_id
                  << ", stepping down" << std::endl;
        return;
    }

    if (response.term < _current_term) {
        // Old response, ignore
        return;
    }

    // Count vote
    if (response.granted) {
        _votes_granted++;
        std::cout << "Node " << _server_id << " received vote from "
                  << peer_id << " (granted), total=" << _votes_granted << std::endl;
    } else {
        _votes_refused++;
        std::cout << "Node " << _server_id << " received vote from "
                  << peer_id << " (refused), total_refused=" << _votes_refused << std::endl;
    }

    // Check if won election
    if (checkElectionWon()) {
        _election_complete.store(true);
        becomeLeaderInternal();  // Call internal version (already holds lock)
    }
}

bool Node::checkElectionWon() {
    // Need majority of all nodes (including self)
    int total_nodes = _peers.size();
    int majority = (total_nodes / 2) + 1;

    bool won = (_votes_granted >= majority);

    if (won) {
        std::cout << "Node " << _server_id << " won election for term "
                  << _current_term << " with " << _votes_granted << "/" << total_nodes
                  << " votes" << std::endl;
    }

    return won;
}

int64_t Node::propose(const std::vector<uint8_t>& data) {
    int64_t index;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        // Only leader can propose
        if (_state != "LEADER") {
            std::cerr << "Node " << _server_id
                      << " is not leader, cannot propose" << std::endl;
            return -1;
        }

        // Append entry to log manager
        LogEntry entry;
        entry.term = _current_term;
        entry.type = 1;  // ENTRY_TYPE_DATA
        entry.data = data;

        index = _log_manager->appendEntry(entry);

        std::cout << "Node " << _server_id << " proposed log at index "
                  << index << " term " << _current_term
                  << " data_size=" << data.size() << std::endl;

        // Update last log index
        _last_log_index = index;
    }

    // Trigger replication to followers (outside lock)
    // Note: In production, this would be async
    triggerReplication();

    return index;
}

void Node::triggerReplication() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_state != "LEADER") {
        return;
    }

    // Replicate to all followers
    for (auto& replicator : _replicators) {
        if (replicator && replicator->isRunning()) {
            replicator->replicateLogs();
        }
    }
}

void Node::updateCommitIndex() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_state != "LEADER") {
        return;
    }

    // Collect all match_index values (including self)
    std::vector<int64_t> match_indices;
    match_indices.push_back(_log_manager->lastLogIndex());  // Self

    for (const auto& replicator : _replicators) {
        if (replicator && replicator->isRunning()) {
            match_indices.push_back(replicator->getMatchIndex());
        }
    }

    // Sort to find majority
    std::sort(match_indices.begin(), match_indices.end());

    // Find the index that is replicated to majority
    // Need at least (N/2 + 1) nodes to have the entry
    size_t majority_index = match_indices.size() / 2;  // Integer division rounds down
    int64_t new_commit_index = match_indices[majority_index];

    // Only update commit index if:
    // 1. New commit index is greater than current
    // 2. The entry at new_commit_index is from current term
    //    (Raft safety property: leader can only commit entries from its own term)
    if (new_commit_index > _commit_index) {
        // Check if the entry at new_commit_index is from current term
        int64_t entry_term = _log_manager->getTerm(new_commit_index);

        if (entry_term == _current_term) {
            int64_t old_commit = _commit_index;
            _commit_index = new_commit_index;

            std::cout << "Node " << _server_id << " updated commit index "
                      << old_commit << " -> " << _commit_index
                      << " (term=" << _current_term << ")" << std::endl;

            // Notify FSMCaller about new commit index
            _fsm_caller->onCommitted(_commit_index);

            // Notify replicators about commit index change
            for (auto& replicator : _replicators) {
                if (replicator && replicator->isRunning()) {
                    replicator->onCommitIndexChange(_commit_index);
                }
            }
        }
    }
}

bool Node::createSnapshot() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_commit_index <= 0) {
        std::cerr << "Node " << _server_id
                  << ": cannot create snapshot, commit_index=" << _commit_index << std::endl;
        return false;
    }

    // Trigger FSM to save snapshot
    bool success = _fsm_caller->triggerSnapshot(_commit_index);

    if (success) {
        // Get term of commit index
        int64_t last_included_term = _log_manager->getTerm(_commit_index);

        // Update LogManager snapshot metadata
        _log_manager->setSnapshot(_commit_index, last_included_term);

        std::cout << "Node " << _server_id << " created snapshot at index "
                  << _commit_index << " term=" << last_included_term << std::endl;
    }

    return success;
}

int64_t Node::addPeer(const std::string& peer_id) {
    int64_t index;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        // Only leader can add peers
        if (_state != "LEADER") {
            std::cerr << "Node " << _server_id
                      << " is not leader, cannot add peer" << std::endl;
            return -1;
        }

        // Check if peer already exists
        for (const auto& peer : _peers) {
            if (peer == peer_id) {
                std::cerr << "Peer " << peer_id << " already exists" << std::endl;
                return -1;
            }
        }

        // Create new peer list
        std::vector<std::string> new_peers = _peers;
        new_peers.push_back(peer_id);

        // Serialize new peer list to data
        std::vector<uint8_t> data;
        for (const auto& peer : new_peers) {
            data.insert(data.end(), peer.begin(), peer.end());
            data.push_back('\n');  // Delimiter
        }

        // Create configuration entry
        LogEntry entry;
        entry.term = _current_term;
        entry.type = 3;  // ENTRY_TYPE_CONFIGURATION
        entry.data = data;

        index = _log_manager->appendEntry(entry);

        std::cout << "Node " << _server_id << " proposed add peer " << peer_id
                  << " at index " << index << " (total peers: " << new_peers.size() << ")" << std::endl;

        _last_log_index = index;
    }

    // Trigger replication (outside lock)
    triggerReplication();

    return index;
}

int64_t Node::removePeer(const std::string& peer_id) {
    int64_t index;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        // Only leader can remove peers
        if (_state != "LEADER") {
            std::cerr << "Node " << _server_id
                      << " is not leader, cannot remove peer" << std::endl;
            return -1;
        }

        // Check if peer exists
        auto it = std::find(_peers.begin(), _peers.end(), peer_id);
        if (it == _peers.end()) {
            std::cerr << "Peer " << peer_id << " not found" << std::endl;
            return -1;
        }

        // Cannot remove self
        if (peer_id == _server_id) {
            std::cerr << "Cannot remove self from cluster" << std::endl;
            return -1;
        }

        // Create new peer list
        std::vector<std::string> new_peers;
        for (const auto& peer : _peers) {
            if (peer != peer_id) {
                new_peers.push_back(peer);
            }
        }

        // Serialize new peer list to data
        std::vector<uint8_t> data;
        for (const auto& peer : new_peers) {
            data.insert(data.end(), peer.begin(), peer.end());
            data.push_back('\n');  // Delimiter
        }

        // Create configuration entry
        LogEntry entry;
        entry.term = _current_term;
        entry.type = 3;  // ENTRY_TYPE_CONFIGURATION
        entry.data = data;

        index = _log_manager->appendEntry(entry);

        std::cout << "Node " << _server_id << " proposed remove peer " << peer_id
                  << " at index " << index << " (total peers: " << new_peers.size() << ")" << std::endl;

        _last_log_index = index;
    }

    // Trigger replication (outside lock)
    triggerReplication();

    return index;
}

void Node::applyConfiguration(const std::vector<std::string>& new_peers) {
    std::lock_guard<std::mutex> lock(_mutex);

    std::cout << "Node " << _server_id << " applying configuration change: "
              << _peers.size() << " -> " << new_peers.size() << " peers" << std::endl;

    // Update peer list
    _peers = new_peers;

    // If we are leader, update replicators
    if (_state == "LEADER") {
        // Stop replicators for removed peers
        _replicators.erase(
            std::remove_if(_replicators.begin(), _replicators.end(),
                [this](const std::shared_ptr<Replicator>& replicator) {
                    if (!replicator) return false;
                    const std::string& peer_addr = replicator->getPeerAddr();
                    // Check if peer still exists
                    bool exists = std::find(_peers.begin(), _peers.end(), peer_addr) != _peers.end();
                    if (!exists) {
                        std::cout << "Stopping replicator for removed peer: " << peer_addr << std::endl;
                        replicator->stop();
                        replicator->join();
                        return true;
                    }
                    return false;
                }),
            _replicators.end());

        // Start replicators for new peers
        for (const auto& peer : _peers) {
            if (peer == _server_id) continue;

            // Check if replicator already exists
            bool exists = false;
            for (auto& replicator : _replicators) {
                if (replicator && replicator->getPeerAddr() == peer) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                std::cout << "Starting replicator for new peer: " << peer << std::endl;
                auto replicator = std::make_shared<Replicator>(peer, this, _current_term);
                replicator->setNextIndex(_log_manager->lastLogIndex() + 1);
                replicator->setMatchIndex(0);

                if (replicator->start()) {
                    _replicators.push_back(replicator);
                }
            }
        }
    }
}

} // namespace v2
} // namespace braft
