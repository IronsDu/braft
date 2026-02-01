// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// v2 module common definitions

#ifndef BRAFT_V2_V2_COMMON_H
#define BRAFT_V2_V2_COMMON_H

/**
 * braft::v2 Namespace
 *
 * This namespace contains the refactored version of braft core modules
 * that use Apache Thrift instead of Protobuf/brpc.
 *
 * The v2 modules coexist with the original modules during the transition period.
 */
namespace braft {
namespace v2 {

// Forward declarations
class Node;
class Replicator;

} // namespace v2
} // namespace braft

#endif // BRAFT_V2_V2_COMMON_H
