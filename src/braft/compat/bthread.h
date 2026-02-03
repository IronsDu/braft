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
// bthread 兼容层，重定向到适配器实现

#ifndef BRAFT_COMPAT_BTHREAD_H
#define BRAFT_COMPAT_BTHREAD_H

#include "braft/compat/timer.h"

// 导入到全局命名空间，使现有代码可以无缝使用
// 将 bthread::compat::bthread_timer_add 导出为 ::bthread_timer_add
using bthread_timer_t = braft::compat::bthread_timer_t;

inline int bthread_timer_add(bthread_timer_t* id,
                             const struct timespec& abstime,
                             void (*on_timer)(void*),
                             void* arg) {
    return braft::compat::bthread_timer_add(id, abstime, on_timer, arg);
}

inline int bthread_timer_del(bthread_timer_t id) {
    return braft::compat::bthread_timer_del(id);
}

#endif  // BRAFT_COMPAT_BTHREAD_H
