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

// braft/compat/bthread.h
// bthread 兼容层 - 重定向到适配器实现
//
// 注意：为了避免与系统 bthread/unstable.h 冲突，不在全局命名空间定义函数
// 请使用 braft::compat::bthread_timer_add() 和 braft::compat::bthread_timer_del()

#ifndef BRAFT_COMPAT_BTHREAD_H
#define BRAFT_COMPAT_BTHREAD_H

#include "braft/compat/timer.h"

// 类型别名，方便使用
using bthread_timer_t = braft::compat::bthread_timer_t;

#endif  // BRAFT_COMPAT_BTHREAD_H
