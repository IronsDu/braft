// Copyright (c) 2026  Braft.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// braft/compat/brpc_adapter.h
// brpc Controller/Closure 的简单适配器

#ifndef BRAFT_COMPAT_BRPC_ADAPTER_H
#define BRAFT_COMPAT_BRPC_ADAPTER_H

#include <string>
#include <memory>

namespace google {
namespace protobuf {

// 前向声明 Closure
class Closure;

}  // namespace protobuf
}  // namespace google

namespace brpc {

// 简单的 Controller 适配器
// 模拟 brpc::Controller 的基本功能
class Controller {
public:
    Controller() : _error_code(0), _failed(false) {}

    void SetFailed(int error_code, const char* reason) {
        _error_code = error_code;
        _failed = true;
        _error_text = reason ? reason : "Unknown error";
    }

    bool Failed() const { return _failed; }

    int ErrorCode() const { return _error_code; }

    const std::string& ErrorText() const { return _error_text; }

private:
    int _error_code;
    bool _failed;
    std::string _error_text;
};

// 简单的 ClosureGuard 适配器
class ClosureGuard {
public:
    explicit ClosureGuard(google::protobuf::Closure* closure)
        : _closure(closure) {}

    ~ClosureGuard() {
        if (_closure) {
            _closure->Run();
        }
    }

    void release() { _closure = NULL; }

private:
    google::protobuf::Closure* _closure;
};

} // namespace brpc

#endif  // BRAFT_COMPAT_BRPC_ADAPTER_H
