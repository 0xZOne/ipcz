// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_CORE_FRAGMENT_ALLOCATOR_H_
#define IPCZ_SRC_CORE_FRAGMENT_ALLOCATOR_H_

#include <atomic>
#include <cstdint>
#include <list>

#include "core/buffer_id.h"
#include "core/fragment.h"
#include "mem/block_allocator.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_map.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"
#include "third_party/abseil-cpp/absl/types/span.h"

namespace ipcz {
namespace core {

// Manages access to a collection of mem::BlockAllocators for the same block
// size, encapsulating the decision of which allocator to use for each
// allocation request. NodeLinkMemory objects use FragmentAllocators for various
// fragment sizes to manage dynamic allocation of smallish shared memory spans.
class FragmentAllocator {
 public:
  explicit FragmentAllocator(uint32_t fragment_size);
  ~FragmentAllocator();

  // Permanently registers a new BlockAllocator with this object, utilizing
  // `memory` for its storage. `buffer_id` is the BufferId associated with the
  // allocator's memory and `buffer_memory` is the full span of bytes mapped by
  // the buffer. `allocator` is an BufferAllocator already initialized over some
  // subset of `buffer_memory`.
  void AddBlockAllocator(BufferId buffer_id,
                         absl::Span<uint8_t> buffer_memory,
                         const mem::BlockAllocator& allocator);

  // Allocates a new fragment. If allocation fails because there is no capacity
  // left in any of the this object's BlockAllocators, this returns null value.
  Fragment Allocate();

  // Releases a fragment back to the allocator.
  void Free(const Fragment& fragment);

 private:
  struct Entry {
    Entry(BufferId buffer_id,
          absl::Span<uint8_t> buffer_memory,
          const mem::BlockAllocator& allocator);
    ~Entry();

    BufferId buffer_id;
    absl::Span<uint8_t> buffer_memory;
    mem::BlockAllocator block_allocator;
    Entry* next = nullptr;
  };

  const uint32_t fragment_size_;

  absl::Mutex mutex_;
  std::list<Entry> entries_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<BufferId, Entry*> entry_map_ ABSL_GUARDED_BY(mutex_);

  // Atomic pointer to the Entry most recently used for successful allocation.
  // This generally only changes when allocation fails and a new allocator must
  // selected.
  std::atomic<Entry*> active_entry_{nullptr};
};

}  // namespace core
}  // namespace ipcz

#endif  // IPCZ_SRC_CORE_FRAGMENT_ALLOCATOR_H_