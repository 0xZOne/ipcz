// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipcz/driver_memory_mapping.h"

#include <algorithm>

namespace ipcz {

DriverMemoryMapping::DriverMemoryMapping() = default;

DriverMemoryMapping::DriverMemoryMapping(const IpczDriver& driver,
                                         IpczDriverHandle mapping_handle,
                                         void* address,
                                         size_t size)
    : driver_(driver),
      mapping_(mapping_handle),
      address_(address),
      size_(size) {}

DriverMemoryMapping::DriverMemoryMapping(DriverMemoryMapping&& other)
    : driver_(other.driver_),
      mapping_(IPCZ_INVALID_DRIVER_HANDLE),
      address_(nullptr),
      size_(0) {
  std::swap(mapping_, other.mapping_);
  std::swap(address_, other.address_);
  std::swap(size_, other.size_);
}

DriverMemoryMapping& DriverMemoryMapping::operator=(
    DriverMemoryMapping&& other) {
  Unmap();
  driver_ = other.driver_;
  std::swap(mapping_, other.mapping_);
  std::swap(address_, other.address_);
  std::swap(size_, other.size_);
  return *this;
}

DriverMemoryMapping::~DriverMemoryMapping() {
  Unmap();
}

void DriverMemoryMapping::Unmap() {
  if (is_valid()) {
    driver_.Close(mapping_, 0, nullptr);
    mapping_ = IPCZ_INVALID_DRIVER_HANDLE;
    address_ = nullptr;
    size_ = 0;
  }
}

}  // namespace ipcz
