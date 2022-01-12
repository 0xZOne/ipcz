// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_CORE_TRAP_SET_H_
#define IPCZ_SRC_CORE_TRAP_SET_H_

#include "core/trap.h"
#include "ipcz/ipcz.h"
#include "mem/ref_counted.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_set.h"

namespace ipcz {
namespace core {

class TrapEventDispatcher;

// A set of Trap objects managed on a single portal.
class TrapSet {
 public:
  TrapSet();
  TrapSet(TrapSet&&);
  TrapSet& operator=(TrapSet&&);
  ~TrapSet();

  void Add(mem::Ref<Trap> trap);
  void Remove(Trap& trap);
  void UpdatePortalStatus(const IpczPortalStatus& status,
                          Trap::UpdateReason reason,
                          TrapEventDispatcher& dispatcher);
  void DisableAllAndClear();

 private:
  absl::flat_hash_set<mem::Ref<Trap>> traps_;
};

}  // namespace core
}  // namespace ipcz

#endif  // IPCZ_SRC_CORE_TRAP_SET_H_
