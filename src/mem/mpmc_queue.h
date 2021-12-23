// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_MEM_MPMC_QUEUE_H_
#define IPCZ_SRC_MEM_MPMC_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "mem/atomic_memcpy.h"

namespace ipcz {
namespace mem {

// MpmcQueue is a multiple-producer, multiple-consumer, bounded, lock-free queue
// structure suitable for use by any number of concurrent producers and
// consumers. MqmcQueue objects do not contain heap references and are safe to
// allocate and use within shared memory regions.
//
// The underlying data type T must be trivially copyable, and consumers should
// pop frequently to avoid starving producers.
template <typename T, size_t kCapacity>
class MpmcQueue {
 public:
  using DataType = T;

  static_assert(kCapacity >= 4 && kCapacity <= 0x80000000,
                "capacity must be between 4 and 2^31, inclusive");
  static_assert(std::is_trivially_copyable<DataType>::value,
                "data must be trivially copyable");

  // The low two bits of the status of each cell. Both zero means the cell is
  // empty; only busy means it's claimed and i the process of being pushed
  // into; only full means it's full and ready to be popped, and both busy and
  // full means it's full and claimed and in the process of being popped out of.
  // A busy status bit allows for the element load/store itself to incur
  // multiple sequence-locked atomic operations without tearing on push/pop.
  static constexpr uint32_t kBusyBit = 1 << 0;
  static constexpr uint32_t kFullBit = 1 << 1;

  // Maximum number of iterations to spin in PushBack or PopFront when
  // encountering spurious failures or losing races with other producers or
  // consumers. Arbitrary smallish number.
  static constexpr size_t kMaxRetries = 10;

  MpmcQueue() = default;
  MpmcQueue(const MpmcQueue&) = delete;
  MpmcQueue& operator=(const MpmcQueue&) = delete;
  ~MpmcQueue() = default;

  // Tries to push `value` onto the tail of the queue. Returns false on failure,
  // either if the queue is full or if we exceed `kMaxRetries` attempts at
  // resolving a race against other producers.
  bool PushBack(const DataType& value) {
    uint32_t tail = tail_.load(std::memory_order_relaxed);
    for (size_t attempts = 0; attempts < kMaxRetries; ++attempts) {
      const uint32_t tail_lap = tail / kCapacity;
      const uint32_t empty_status = tail_lap << 2;
      Cell& c = cells_[tail % kCapacity];
      uint32_t status = empty_status;
      if (c.status.compare_exchange_weak(status, empty_status | kBusyBit,
                                         std::memory_order_release,
                                         std::memory_order_relaxed)) {
        // Successfully claimed the slot. Copy in the data, increment the tail
        // index if necessary, and mark the slot as full and no longer busy.
        AtomicWriteMemcpy(&c.data, &value, sizeof(c.data));
        tail_.compare_exchange_strong(tail, tail + 1,
                                      std::memory_order_relaxed);
        c.status.store(empty_status | kFullBit, std::memory_order_release);
        return true;
      }

      // If this was a spurious CAS failure, simply retry as-is.
      if (status == empty_status) {
        continue;
      }

      // If the cell was already full and is on the lap before our current tail
      // lap, we've just bumped into the head. The queue is full.
      const uint32_t next_cell_lap = (status >> 2) + 1;
      if (next_cell_lap == tail_lap && (status & kFullBit) != 0) {
        return false;
      }

      // Otherwise we've just lost a race. Reload the tail index and try again.
      tail = tail_.load(std::memory_order_relaxed);
    }
    return false;
  }

  // Tries to pop a value off the front of the queue and into `value`. Returns
  // false on failure, either if the queue is empty or we exceed `kMaxRetries`
  // attempts at resolving a race against other consumers.
  bool PopFront(DataType& value) {
    uint32_t head = head_.load(std::memory_order_relaxed);
    for (size_t attempts = 0; attempts < kMaxRetries; ++attempts) {
      const uint32_t head_lap = head / kCapacity;
      const uint32_t full_status = (head_lap << 2) | kFullBit;
      Cell& c = cells_[head % kCapacity];
      uint32_t status = full_status;
      if (c.status.compare_exchange_weak(status, full_status | kBusyBit,
                                         std::memory_order_release,
                                         std::memory_order_relaxed)) {
        // Successfully claimed the slot.
        AtomicReadMemcpy(&value, &c.data, sizeof(value));
        head_.compare_exchange_strong(head, head + 1,
                                      std::memory_order_relaxed);

        c.status.store((head_lap + 1) << 2, std::memory_order_release);
        return true;
      }

      // Spurious failure. Retry as-is.
      if (status == full_status) {
        continue;
      }

      // If kFullBit is not set and the cell is still on our current head lap,
      // the slot is empty and there are no populated slots which follow it; so
      // the queue itself is empty.
      const uint32_t current_cell_lap = status >> 2;
      if (!(status & kFullBit) && current_cell_lap == head_lap) {
        return false;
      }

      // Otherwise we've just lost a race. Reload the tail index and try again.
      head = head_.load(std::memory_order_relaxed);
    }
    return false;
  }

 private:
  struct Cell {
    DataType data;
    std::atomic<uint32_t> status{0};
  };

  // Index of the current head of the queue in `cells_`. Only written by
  // consumers (PopBack()).
  std::atomic<uint32_t> head_{0};

  // Index of the current tail of the queue in `cells_`. Only written by
  // producers (PushBack()).
  std::atomic<uint32_t> tail_{0};

  Cell cells_[kCapacity];
};

}  // namespace mem
}  // namespace ipcz

#endif  // IPCZ_SRC_MEM_MPMC_QUEUE_H_
