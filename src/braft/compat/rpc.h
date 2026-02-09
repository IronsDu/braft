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

// braft/compat/rpc.h
// 抽象 RPC 接口，用于替换 brpc::Channel 和 brpc::Controller

#ifndef BRAFT_COMPAT_RPC_H
#define BRAFT_COMPAT_RPC_H

#include <stdint.h>
#include <string>

namespace braft {
namespace compat {

// RPC 调用 ID（用于替换 brpc::CallId）
struct RpcCallId {
    uint64_t value;

    RpcCallId() : value(0) {}
    explicit RpcCallId(uint64_t v) : value(v) {}

    bool is_valid() const { return value != 0; }
};

// RPC 控制器抽象接口
// 用于替换 brpc::Controller
class IRpcController {
public:
    virtual ~IRpcController() {}

    // 设置超时时间（毫秒）
    virtual void set_timeout_ms(int timeout_ms) = 0;

    // 获取超时时间
    virtual int timeout_ms() const = 0;

    // 检查 RPC 是否失败
    virtual bool Failed() const = 0;

    // 设置失败状态
    virtual void SetFailed(int error_code, const char* error_text) = 0;

    // 获取错误文本
    virtual std::string ErrorText() const = 0;

    // 获取调用 ID
    virtual RpcCallId call_id() const = 0;

    // 获取远程地址
    virtual std::string remote_side() const = 0;
};

// RPC 回调抽象接口
// 用于替换 google::protobuf::Closure
class IClosure {
public:
    virtual ~IClosure() {}

    virtual void Run() = 0;
};

// RPC 通道抽象接口
// 用于替换 brpc::Channel
class IRpcChannel {
public:
    virtual ~IRpcChannel() {}

    // 初始化通道
    // address: 服务器地址（格式：host:port）
    // 返回：0 表示成功，-1 表示失败
    virtual int Init(const std::string& address) = 0;

    // 检查通道是否已初始化
    virtual bool IsInitialized() const = 0;

    // 获取地址
    virtual const std::string& address() const = 0;
};

// RPC 服务器抽象接口
// 用于替换 brpc::Server
class IRpcServer {
public:
    virtual ~IRpcServer() {}

    // 启动服务器
    // port: 监听端口
    // 返回：0 表示成功，-1 表示失败
    virtual int Start(int port) = 0;

    // 停止服务器
    // 返回：0 表示成功，-1 表示失败
    virtual int Stop() = 0;

    // 等待服务器完全停止
    // 返回：0 表示成功，-1 表示失败
    virtual int Join() = 0;

    // 检查服务器是否正在运行
    virtual bool IsRunning() const = 0;

    // 获取监听端口
    virtual int port() const = 0;
};

// 通道选项（用于替换 brpc::ChannelOptions）
struct ChannelOptions {
    int connect_timeout_ms;  // 连接超时
    int timeout_ms;          // RPC 超时（-1 表示永不超时）

    ChannelOptions()
        : connect_timeout_ms(200)
        , timeout_ms(-1) {}
};

// 服务器选项（用于替换 brpc::ServerOptions）
struct ServerOptions {
    int max_concurrency;     // 最大并发数
    int num_threads;         // 工作线程数

    ServerOptions()
        : max_concurrency(0)
        , num_threads(8) {}
};

}  // namespace compat
}  // namespace braft

#endif  // BRAFT_COMPAT_RPC_H
