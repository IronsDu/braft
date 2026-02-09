// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// File transfer service for snapshot copying
// Maps to braft/file_service.proto

namespace cpp braft

/**
 * GetFileRequest - Request to read a file chunk
 * Used for snapshot file transfer
 */
struct GetFileRequest {
  1: i64 reader_id,    // File reader ID identifying the session
  2: string filename,  // Path to the file
  3: i64 count,        // Number of bytes to read
  4: i64 offset,       // Offset in the file to start reading
  5: optional bool read_partly  // Allow partial read
}

/**
 * GetFileResponse - Response with file chunk data
 * Actual file data is transported in attachment for efficiency
 */
struct GetFileResponse {
  1: bool eof,                 // True if this is the last chunk
  2: optional i64 read_size    // Number of bytes actually read
}

/**
 * FileService - File transfer service for snapshot copying
 *
 * This service handles streaming file transfers from snapshot reader to
 * remote copier. The actual file data is transported via fbthrift's
 * attachment mechanism, not in the response structure.
 *
 * For streaming support, fbthrift can use the `stream` keyword in future
 * iterations to enable true bidirectional streaming.
 *
 * Usage flow:
 * 1. Snapshot copier requests a file with offset and count
 * 2. Snapshot reader sends back chunk data in attachment
 * 3. Repeat until eof is true
 */
service FileService {
  /**
   * getFile - Read a chunk of file data
   * File data is returned in attachment
   */
  GetFileResponse getFile(1: GetFileRequest req)
}
