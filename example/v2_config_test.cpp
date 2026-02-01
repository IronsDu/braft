// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Configuration Change Test - Test addPeer/removePeer functionality

#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

#include "braft/v2/node.h"

using namespace braft::v2;

/**
 * Test configuration changes:
 * 1. Start 3-node cluster
 * 2. Wait for leader election
 * 3. Add a new peer
 * 4. Remove a peer
 * 5. Create snapshot
 * 6. Verify cluster state
 */
int main(int argc, char* argv[]) {
    std::cout << "===============================================" << std::endl;
    std::cout << "Braft v2 Configuration Change Test" << std::endl;
    std::cout << "===============================================\n" << std::endl;

    // Configuration
    std::string group_id = "test_group";
    int num_nodes = 3;
    int base_port = 8090;  // Use different ports to avoid conflict

    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<std::string> peers;

    // Build initial peer list
    for (int i = 0; i < num_nodes; ++i) {
        std::string peer = "127.0.0.1:" + std::to_string(base_port + i) + ":0";
        peers.push_back(peer);
    }

    // Step 1: Start all nodes
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

    // Step 2: Wait for leader election
    std::cout << "\nStep 2: Waiting for leader election..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Find leader
    Node* leader = nullptr;
    int leader_index = -1;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]->isLeader()) {
            leader = nodes[i].get();
            leader_index = i;
            std::cout << "  - Leader: Node " << i << " ("
                      << nodes[i]->getServerId() << ")" << std::endl;
            break;
        }
    }

    if (!leader) {
        std::cerr << "ERROR: No leader elected!" << std::endl;
        for (auto& node : nodes) {
            node->shutdown();
        }
        return 1;
    }

    // Step 3: Propose some normal entries
    std::cout << "\nStep 3: Proposing normal entries..." << std::endl;
    for (int i = 0; i < 3; ++i) {
        std::string data_str = "Test entry #" + std::to_string(i + 1);
        std::vector<uint8_t> data(data_str.begin(), data_str.end());

        int64_t index = leader->propose(data);
        if (index > 0) {
            std::cout << "  - Proposed entry " << (i + 1)
                      << " at index " << index << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Step 4: Add a new peer
    std::cout << "\nStep 4: Adding new peer..." << std::endl;
    std::string new_peer = "127.0.0.1:" + std::to_string(base_port + num_nodes) + ":0";
    std::cout << "  - Adding peer: " << new_peer << std::endl;

    int64_t config_index = leader->addPeer(new_peer);
    if (config_index > 0) {
        std::cout << "  - Configuration change proposed at index " << config_index << std::endl;
    } else {
        std::cerr << "  - Failed to propose configuration change" << std::endl;
    }

    // Wait for configuration to be applied
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Step 5: Create snapshot on leader
    std::cout << "\nStep 5: Creating snapshot..." << std::endl;
    bool snapshot_created = leader->createSnapshot();
    if (snapshot_created) {
        std::cout << "  - Snapshot created successfully" << std::endl;
    } else {
        std::cerr << "  - Failed to create snapshot" << std::endl;
    }

    // Step 6: Remove a peer
    std::cout << "\nStep 6: Removing a peer..." << std::endl;
    if (num_nodes > 1) {
        std::string peer_to_remove = peers[1];  // Remove second node
        std::cout << "  - Removing peer: " << peer_to_remove << std::endl;

        config_index = leader->removePeer(peer_to_remove);
        if (config_index > 0) {
            std::cout << "  - Configuration change proposed at index " << config_index << std::endl;
        } else {
            std::cerr << "  - Failed to propose configuration change" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    // Step 7: Print final cluster state
    std::cout << "\nStep 7: Final cluster state:" << std::endl;
    for (size_t i = 0; i < nodes.size(); ++i) {
        std::cout << "  - Node " << i << ": " << nodes[i]->getState()
                  << " (term=" << nodes[i]->getCurrentTerm()
                  << ", commit_index=" << nodes[i]->getCommitIndex() << ")" << std::endl;
    }

    // Print log statistics
    std::cout << "\nLog statistics:" << std::endl;
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto* log_mgr = nodes[i]->getLogManager();
        if (log_mgr) {
            auto snapshot = log_mgr->getSnapshot();
            std::cout << "  - Node " << i << ": last_log_index="
                      << log_mgr->lastLogIndex()
                      << ", snapshot_index=" << snapshot.first << std::endl;
        }
    }

    // Summary
    std::cout << "\n===============================================" << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << "  - Initial nodes: " << num_nodes << std::endl;
    std::cout << "  - Leader: Node " << leader_index << std::endl;
    std::cout << "  - Peer added: " << new_peer << std::endl;
    if (num_nodes > 1) {
        std::cout << "  - Peer removed: " << peers[1] << std::endl;
    }
    std::cout << "  - Snapshot created: " << (snapshot_created ? "Yes" : "No") << std::endl;
    std::cout << "  - Final commit index: " << leader->getCommitIndex() << std::endl;
    std::cout << "===============================================\n" << std::endl;

    // Keep running for observation
    std::cout << "Nodes will continue running for 3 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Shutdown
    std::cout << "\nShutting down all nodes..." << std::endl;
    for (auto& node : nodes) {
        node->shutdown();
    }

    std::cout << "\nConfiguration change test completed. Goodbye!" << std::endl;
    return 0;
}
