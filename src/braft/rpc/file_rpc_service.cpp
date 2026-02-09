// Copyright (c) 2026 Braft Refactor Project
// Licensed under the Apache License, Version 2.0

#include "braft/rpc/file_rpc_service.h"

namespace braft {

void FileRpcService::getFile(GetFileResponse& _return,
                              const GetFileRequest& req) {
    // TODO: Implement file reading
    // This requires:
    // 1. Looking up the file reader by reader_id
    // 2. Reading the requested chunk from the file
    // 3. Writing data to attachment
    // 4. Setting response metadata

    // For now, return a default response
    _return.eof = true;
    _return.read_size = 0;
}

} // namespace braft
