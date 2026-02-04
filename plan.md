# Braft Refactor Plan: 从 brpc 迁移到标准库 + Thrift

## 概述

本文档记录将 braft 从 brpc/butil/bthread 依赖迁移到标准库 + Thrift 的完整重构计划和实现细节。

---

## 总体目标

- **移除依赖**：完全移除对 brpc、butil、bthread 的依赖
- **新依赖**：保留 Thrift（RPC框架+序列化），使用标准库和 folly（基础库）
- **🔴 核心原则**：**保持 Raft 算法语义不变**，只替换底层依赖组件
- **实施方案**：采用适配器模式，创建抽象接口层
- **保证正确性**：通过测试用例验证 Raft 协议的正确性

---

## 分阶段实施计划（适配器方案）

### 阶段 0：准备工作 ✅

**目标**：建立开发环境和基础设施

**任务**：
1. 创建独立的开发分支 `refactor/fbthrift-folly`
2. 设置 Thrift 和 folly 的开发环境
3. 验证现有测试用例的运行情况（建立基准）
4. 分析 brpc API 的具体使用点

**完成标准**：
- 开发环境可用，能够编译
- 现有测试用例可以运行并记录通过率

---

### 阶段 1A：设计抽象接口层 ✅

**目标**：定义所有需要的抽象接口

**完成日期**：2026-02-03

**完成内容**：

1. **任务队列抽象** (`src/braft/compat/task_queue.h`)
   - `ITaskQueue<T>` - 任务队列接口 ✅
   - `StdTaskQueue<T>` - 标准库实现（header-only 模板类）✅
   - `TaskHandler<T>` - 支持 context 参数的任务处理器函数 ✅

2. **定时器抽象** (`src/braft/compat/timer.h`)
   - `ITimerManager` - 定时器接口 ✅
   - `StdTimerManager` - 标准库实现 ✅
   - `TimerId` - 定时器 ID 类型 ✅
   - 全局定时器辅助函数 ✅

3. **RPC 抽象层** (`src/braft/compat/rpc.h`)
   - `IRpcController` - RPC 控制器抽象 ✅
   - `IRpcChannel` - RPC 通道抽象 ✅
   - `IClosure` - 回调抽象 ✅

4. **brpc 适配器** (`src/braft/compat/brpc_adapter.h`)
   - `Controller` - brpc::Controller 的简单适配器 ✅
   - `ClosureGuard` - RAII 闭包管理 ✅

---

### 阶段 2A：替换低耦合模块 ✅

**目标**：从低耦合模块开始替换，验证抽象接口可行性

**完成日期**：2026-02-03

**完成的模块**：

| 模块 | 状态 | 主要修改 |
|------|------|----------|
| FSMCaller | ✅ 完成 | `bthread::ExecutionQueue` → `ITaskQueue<ApplyTask>` |
| LogManager | ✅ 完成 | `bthread::ExecutionQueue` → `ITaskQueue<StableClosure*>` |

**主要修改内容**：

1. **FSMCaller** (`src/braft/fsm_caller.h/cpp`)
   - 替换 `bthread::ExecutionQueueId<ApplyTask> _queue_id` 为 `ITaskQueue<ApplyTask>* _task_queue`
   - 修改 `run` 函数签名：`run(void* context, ApplyTask* tasks, size_t count)`
   - 更新所有 `bthread::execution_queue_execute` 调用为 `_task_queue->execute`

2. **LogManager** (`src/braft/log_manager.h/cpp`)
   - 替换 `bthread::ExecutionQueueId<StableClosure*> _disk_queue` 为 `ITaskQueue<StableClosure*>* _disk_queue`
   - 更新 `disk_thread` 签名为 `disk_task_handler(void* context, StableClosure** tasks, size_t count)`
   - 更新所有 `bthread::execution_queue_execute` 调用为 `_disk_queue->execute`

---

### 阶段 3A：替换中等耦合模块 🔄 进行中

**目标**：替换 Node 和 Replicator 的底层组件

**开始日期**：2026-02-03
**最新更新**：2026-02-04

**已完成的工作**：

1. **定时器适配器实现** ✅ (2026-02-04 完成)
   - 实现 `bthread_timer_t` 兼容层
   - 更新 node.cpp, remote_file_copier.cpp, repeated_timer_task.cpp, replicator.cpp
   - 所有 `bthread_timer_add/del` 改为 `compat::bthread_timer_add/del`

2. **类型冲突解决** ✅ (2026-02-04 完成)
   - 重命名 Thrift 类型避免与 Protobuf 冲突：
     - `EntryType` → `ThriftEntryType`
     - `ErrorType` → `ThriftErrorType`
     - `RaftError` → `ThriftRaftError`
   - 更新 v2 模块使用新类型名

**编译状态**：
- ✅ `libbraft_compat.a` 编译成功
- ✅ `libbraft.a` 编译成功（legacy braft）
- ✅ `libbraft.so` 编译成功
- ✅ `braft_cli` 编译成功

---

### 阶段 4A：替换高耦合模块（待开始）

**目标**：替换 Replicator

**任务**：
1. 替换 Replicator 的 brpc::Channel
2. 替换 Replicator 的定时器（已在 3A 完成）
3. 处理异步 RPC 调用的适配

**完成标准**：
- Replicator 使用抽象接口
- 日志复制功能正常
- 性能测试通过

---

### 阶段 5A：清理和优化（待开始）

**目标**：移除所有 brpc/butil/bthread 依赖

**任务**：
1. 删除所有 brpc/butil/bthread 的 include
2. 更新 CMakeLists.txt 移除相关依赖
3. 性能优化（如果需要）
4. 完整的集成测试

**完成标准**：
- 无 brpc/butil/bthread 依赖
- 所有测试通过
- 性能满足要求

---

## 关键设计决策记录

### 🔴 决策 0：采用适配器模式（2026-02-03 更新）

**选择**：使用适配器模式，创建抽象接口层，保持 Raft 算法语义不变

**理由**：
1. **保持算法正确性**：Raft 算法逻辑经过充分验证，不应重写
2. **降低风险**：只替换基础设施层，不影响核心算法
3. **渐进式迁移**：可以逐步替换每个模块，每步都可以验证

**架构设计**：
```
┌─────────────────────────────────────────┐
│         braft 核心算法逻辑               │
│    (保持不变！node.cpp, replicator.cpp)  │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│     抽象接口层（适配器）                  │
│  IRpcChannel, ITaskQueue, ITimer        │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│     新底层实现                           │
│  StdTaskQueue, StdTimerManager, ...    │
└─────────────────────────────────────────┘
```

**核心原则**：
- ✅ Raft 算法逻辑保持不变
- ✅ 只替换 brpc/butil/bthread 等底层组件
- ✅ 通过抽象接口层隔离变化

---

## 类型冲突解决方案（2026-02-04 完成）

### 问题

Thrift 生成的 `EntryType`, `ErrorType`, `RaftError` 与 Protobuf 生成的同名枚举冲突。

### 解决方案

修改 `idl/base.thrift`，重命名 Thrift 类型：
- `EntryType` → `ThriftEntryType`
- `ErrorType` → `ThriftErrorType`
- `RaftError` → `ThriftRaftError`

### 更新的文件
- `idl/base.thrift` - 重命名类型定义
- `src/braft/v2/replicator.cpp` - 使用 `ThriftEntryType`
- `src/braft/v2/log_manager.h` - 更新注释

---

## bthread 链接冲突解决方案（2026-02-04 完成）

### 问题

系统的 `bthread/unstable.h` 声明的 `bthread_timer_add/del` 与 `compat/bthread.h` 的函数冲突。

### 解决方案

修改 `src/braft/compat/bthread.h`：
- 移除全局命名空间的函数定义
- 只保留类型别名 `using bthread_timer_t`
- 使用时需显式调用 `compat::bthread_timer_add/del`

### 更新的文件
- `src/braft/compat/bthread.h` - 移除全局函数
- `src/braft/node.cpp` - 使用 `compat::bthread_timer_add/del`
- `src/braft/remote_file_copier.cpp` - 使用 `compat::bthread_timer_add/del`
- `src/braft/repeated_timer_task.cpp` - 使用 `compat::bthread_timer_add/del`
- `src/braft/replicator.cpp` - 使用 `compat::bthread_timer_add/del`

---

## 风险和挑战

### 技术风险

1. **异步模型差异**
   - bthread 的协程调度与 std::thread 不同
   - **缓解措施**：使用任务队列抽象隔离差异

2. **错误处理**
   - Thrift 的异常处理与 brpc 的错误码机制不同
   - **缓解措施**：在 RPC 封装层统一错误处理

3. **序列化兼容性**
   - Protobuf 和 Thrift 的序列化格式不同
   - **缓解措施**：通过类型重命名解决冲突

---

## 当前进度（2026-02-04 更新）

- [x] 阶段 0：准备工作 ✅
- [x] 阶段 1A：抽象接口层 ✅
- [x] 阶段 2A：FSMCaller/LogManager 适配器 ✅
- [🔄] 阶段 3A：Node/Replicator 定时器适配 ✅ (bthread_timer 部分)
- [ ] 阶段 4A：Replicator RPC 适配
- [ ] 阶段 5A：清理和优化

---

## 环境信息（2026-02-04）

**分支**: `refactor/fbthrift-folly`
**编译器**: GCC 15.2.1 (支持 C++20)
**操作系统**: Fedora 43 (Linux 6.17.1)

### 已安装依赖
- **gcc-c++**: 15.2.1
- **cmake**: 构建工具
- **leveldb-devel**: libleveldb-dev
- **gflags-devel**: libgflags-dev
- **protobuf-devel**: libprotobuf-dev 3.19.6
- **thrift-devel**: thrift-devel 0.20.0
- **glog-devel**: libglog-dev
- **openssl-devel**: openssl-devel 3.5.4
- **brpc**: 1.6.0 (从源码编译，安装到 /usr/local)

### 编译状态（2026-02-04）

```
✅ libbraft_compat.a     (557 KB)  - 适配器模块库
✅ libbraft.a            (legacy braft 静态库)
✅ libbraft.so           (legacy braft 共享库)
✅ braft_cli             (命令行工具)
```

---

## 参考资料

- [Braft 官方文档](https://github.com/brpc/braft)
- [Apache Thrift 文档](https://thrift.apache.org/)
- [Raft 论文](https://raft.github.io/raft.pdf)

---

**文档更新日期**：2026-02-04
