// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0
// File RPC Service - Thrift-based implementation for file transfer

#ifndef BRAFT_RPC_FILE_RPC_SERVICE_H
#define BRAFT_RPC_FILE_RPC_SERVICE_H

#include "FileService.h"

namespace braft {

/**
 * FileRpcService - Implementation of FileServiceIf
 *
 * Handles file transfer requests for snapshot copying.
 */
class FileRpcService : public FileServiceIf {
public:
    FileRpcService() = default;
    ~FileRpcService() override = default;

    /**
     * getFile - Handle file read request
     * @param _return Response to fill (eof, read_size)
     * @param req File request with reader_id, filename, count, offset
     *
     * Actual file data should be written to attachment.
     */
    void getFile(GetFileResponse& _return,
                 const GetFileRequest& req) override;
};

} // namespace braft

#endif // BRAFT_RPC_FILE_RPC_SERVICE_H
