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

// braft/compat/brpc_channel_adapter.h
// brpc::Channel 适配器 - 实现 Protobuf RpcChannel 接口，内部使用 Thrift RPC

#ifndef BRAFT_COMPAT_BRPC_CHANNEL_ADAPTER_H
#define BRAFT_COMPAT_BRPC_CHANNEL_ADAPTER_H

#include <string>
#include <memory>

namespace google {
namespace protobuf {

class Message;
class RpcController;
class Closure;
class MethodDescriptor;

}  // namespace protobuf
}  // namespace google

namespace braft {
namespace compat {

/**
 * BrpcRpcChannelAdapter - Protobuf RpcChannel 的适配器
 *
 * 实现 Protobuf 的 RpcChannel 接口，内部使用 Thrift RPC 客户端
 * 用于替换 brpc::Channel，支持 Replicator 的异步 RPC 调用
 *
 * 工作流程：
 * 1. CallMethod() 被调用（来自 RaftService_Stub）
 * 2. 将 Protobuf Message 序列化为字节流
 * 3. 在后台线程中调用 Thrift RPC（同步）
 * 4. 将 Thrift 响应反序列化为 Protobuf Message
 * 5. 触发用户提供的 done 回调
 */
class BrpcRpcChannelAdapter : public ::google::protobuf::RpcChannel {
public:
    BrpcRpcChannelAdapter();
    ~BrpcRpcChannelAdapter();

    // 禁止拷贝
    BrpcRpcChannelAdapter(const BrpcRpcChannelAdapter&) = delete;
    BrpcRpcChannelAdapter& operator=(const BrpcRpcChannelAdapter&) = delete;

    /**
     * 初始化 Channel
     * @param server_addr 服务器地址 "host:port"
     * @param timeout_ms 超时时间（毫秒）
     * @return 0 成功，其他值失败
     */
    int Init(const std::string& server_addr, int timeout_ms = 5000);

    /**
     * 实现 RpcChannel::CallMethod
     * 这是一个异步接口，在内部使用线程池执行同步 Thrift 调用
     */
    void CallMethod(const ::google::protobuf::MethodDescriptor* method,
                   ::google::protobuf::RpcController* controller,
                   const ::google::protobuf::Message* request,
                   ::google::protobuf::Message* response,
                   ::google::protobuf::Closure* done) override;

    /**
     * 实现 RpcChannel::GetRequestPrototype
     */
    const ::google::protobuf::Message& GetRequestPrototype(
            const ::google::protobuf::MethodDescriptor* method) const override;

    /**
     * 实现 RpcChannel::GetResponsePrototype
     */
    const ::google::protobuf::Message& GetResponsePrototype(
            const ::google::protobuf::MethodDescriptor* method) const override;

private:
    std::string _server_addr;
    int _timeout_ms;
    bool _initialized;
};

} // namespace compat
} // namespace braft

#endif  // BRAFT_COMPAT_BRPC_CHANNEL_ADAPTER_H
