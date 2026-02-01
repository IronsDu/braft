// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Raft v2 Test - Leader election and log replication

#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

#include "braft/v2/node.h"

using namespace braft::v2;

/**
 * Simple test for Raft v2 implementation
 * Tests:
 * 1. Multi-node startup
 * 2. Leader election
 * 3. Log proposal and replication
 * 4. Majority commit
 */
int main(int argc, char* argv[]) {
    std::cout << "===============================================" << std::endl;
    std::cout << "Braft v2 Raft Test" << std::endl;
    std::cout << "===============================================\n" << std::endl;

    // Configuration
    std::string group_id = "test_group";
    int num_nodes = 3;
    int base_port = 8080;

    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<std::string> peers;

    // Build peer list
    for (int i = 0; i < num_nodes; ++i) {
        std::string peer = "127.0.0.1:" + std::to_string(base_port + i) + ":0";
        peers.push_back(peer);
    }

    // Create and start all nodes
    std::cout << "Step 1: Starting " << num_nodes << " nodes..." << std::endl;
    for (int i = 0; i < num_nodes; ++i) {
        std::string server_id = peers[i];
        auto node = std::make_shared<Node>(group_id, server_id, peers);

        int port = base_port + i;
        if (!node->start(port)) {
            std::cerr << "Failed to start node " << server_id << std::endl;
            return 1;
        }

        nodes.push_back(node);
        std::cout << "  - Node " << i << " (" << server_id
                  << ") started as " << node->getState() << std::endl;
    }

    // Wait for election
    std::cout << "\nStep 2: Waiting for leader election..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Find leader
    Node* leader = nullptr;
    int leader_index = -1;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]->isLeader()) {
            leader = nodes[i].get();
            leader_index = i;
            std::cout << "  - Leader found: Node " << i << " ("
                      << nodes[i]->getServerId() << ")" << std::endl;
            break;
        }
    }

    if (!leader) {
        std::cout << "  - No leader elected yet (waiting longer)..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));

        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i]->isLeader()) {
                leader = nodes[i].get();
                leader_index = i;
                std::cout << "  - Leader found: Node " << i << " ("
                          << nodes[i]->getServerId() << ")" << std::endl;
                break;
            }
        }
    }

    if (!leader) {
        std::cerr << "ERROR: No leader elected!" << std::endl;
        // Cleanup and exit
        for (auto& node : nodes) {
            node->shutdown();
        }
        return 1;
    }

    // Print all node states
    std::cout << "\nCluster state:" << std::endl;
    for (size_t i = 0; i < nodes.size(); ++i) {
        std::cout << "  - Node " << i << ": " << nodes[i]->getState()
                  << " (term=" << nodes[i]->getCurrentTerm() << ")" << std::endl;
    }

    // Test log proposal
    std::cout << "\nStep 3: Testing log proposal..." << std::endl;
    const int num_entries = 5;

    for (int i = 0; i < num_entries; ++i) {
        std::string data_str = "Test entry #" + std::to_string(i + 1);
        std::vector<uint8_t> data(data_str.begin(), data_str.end());

        int64_t index = leader->propose(data);
        if (index > 0) {
            std::cout << "  - Proposed entry " << (i + 1)
                      << " at index " << index << std::endl;
        } else {
            std::cerr << "  - Failed to propose entry " << (i + 1) << std::endl;
        }

        // Small delay between proposals
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Wait for replication
    std::cout << "\nStep 4: Waiting for log replication..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Print log manager stats
    std::cout << "\nLog statistics:" << std::endl;
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto* log_mgr = nodes[i]->getLogManager();
        if (log_mgr) {
            std::cout << "  - Node " << i << ": last_log_index="
                      << log_mgr->lastLogIndex()
                      << ", last_log_term=" << log_mgr->lastLogTerm() << std::endl;
        }
    }

    // Print commit indices
    std::cout << "\nCommit indices:" << std::endl;
    for (size_t i = 0; i < nodes.size(); ++i) {
        std::cout << "  - Node " << i << ": commit_index="
                  << nodes[i]->getCommitIndex() << std::endl;
    }

    // Test update commit index on leader
    std::cout << "\nStep 5: Testing commit index update..." << std::endl;
    leader->updateCommitIndex();

    // Final state
    std::cout << "\nFinal cluster state:" << std::endl;
    for (size_t i = 0; i < nodes.size(); ++i) {
        std::cout << "  - Node " << i << ": " << nodes[i]->getState()
                  << " (term=" << nodes[i]->getCurrentTerm()
                  << ", commit_index=" << nodes[i]->getCommitIndex() << ")" << std::endl;
    }

    // Summary
    std::cout << "\n===============================================" << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << "  - Total nodes: " << num_nodes << std::endl;
    std::cout << "  - Leader: Node " << leader_index << std::endl;
    std::cout << "  - Entries proposed: " << num_entries << std::endl;

    int leader_commit = leader->getCommitIndex();
    std::cout << "  - Leader commit index: " << leader_commit << std::endl;

    // Check if majority committed
    int committed_count = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]->getCommitIndex() >= leader_commit) {
            committed_count++;
        }
    }

    int majority = (num_nodes / 2) + 1;
    if (committed_count >= majority) {
        std::cout << "  - Result: SUCCESS (majority committed)" << std::endl;
    } else {
        std::cout << "  - Result: PARTIAL (only " << committed_count
                  << "/" << num_nodes << " committed)" << std::endl;
    }
    std::cout << "===============================================\n" << std::endl;

    // Keep running for a bit to observe
    std::cout << "Nodes will continue running for 5 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Shutdown all nodes
    std::cout << "\nShutting down all nodes..." << std::endl;
    for (auto& node : nodes) {
        node->shutdown();
        std::cout << "  - Node " << node->getServerId() << " stopped" << std::endl;
    }

    std::cout << "\nTest completed. Goodbye!" << std::endl;
    return 0;
}
