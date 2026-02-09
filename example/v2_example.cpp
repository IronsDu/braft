// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// Simple example demonstrating v2::Node usage

#include <iostream>
#include <memory>
#include <vector>

#include "braft/v2/node.h"

int main(int argc, char* argv[]) {
    std::cout << "Braft v2 Example - Thrift-based Raft Node" << std::endl;
    std::cout << "===========================================" << std::endl;

    // Create a Raft node
    std::string group_id = "test_group";
    std::string server_id = "127.0.0.1:8080:0";
    std::vector<std::string> peers;
    peers.push_back("127.0.0.1:8080:0");
    peers.push_back("127.0.0.1:8081:0");
    peers.push_back("127.0.0.1:8082:0");

    auto node = std::make_shared<braft::v2::Node>(
        group_id, server_id, peers);

    // Start the node
    std::cout << "Starting node on port 8080..." << std::endl;
    if (!node->start(8080)) {
        std::cerr << "Failed to start node!" << std::endl;
        return 1;
    }

    std::cout << "Node started successfully!" << std::endl;
    std::cout << "Group ID: " << node->getGroupId() << std::endl;
    std::cout << "Server ID: " << node->getServerId() << std::endl;
    std::cout << "State: " << node->getState() << std::endl;
    std::cout << "Term: " << node->getCurrentTerm() << std::endl;

    // Keep the node running
    std::cout << "\nNode is running. Press Enter to shutdown..." << std::endl;
    std::cin.get();

    // Shutdown the node
    std::cout << "Shutting down node..." << std::endl;
    node->shutdown();
    std::cout << "Node stopped." << std::endl;

    return 0;
}
