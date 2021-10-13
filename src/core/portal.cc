// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/portal.h"

#include <memory>
#include <utility>

#include "core/direct_portal_backend.h"
#include "core/name.h"
#include "core/node.h"
#include "core/portal_backend.h"
#include "core/routed_portal_backend.h"
#include "third_party/abseil-cpp/absl/base/macros.h"

namespace ipcz {
namespace core {

Portal::Portal(std::unique_ptr<PortalBackend> backend) {
  SetBackend(std::move(backend));
}

Portal::~Portal() = default;

// static
Portal::Pair Portal::CreateLocalPair(mem::Ref<Node> node) {
  DirectPortalBackend::Pair backends =
      DirectPortalBackend::CreatePair(std::move(node));
  auto portal0 = std::make_unique<Portal>(std::move(backends.first));
  auto portal1 = std::make_unique<Portal>(std::move(backends.second));
  return {std::move(portal0), std::move(portal1)};
}

// static
std::unique_ptr<Portal> Portal::CreateRouted(mem::Ref<Node> node) {
  return std::make_unique<Portal>(std::make_unique<RoutedPortalBackend>(
      std::move(node), PortalName(Name::kRandom)));
}

std::unique_ptr<PortalBackend> Portal::TakeBackend() {
  absl::MutexLock lock(&mutex_);
  backend_->set_portal(nullptr);
  return std::move(backend_);
}

void Portal::SetBackend(std::unique_ptr<PortalBackend> backend) {
  absl::MutexLock lock(&mutex_);
  ABSL_ASSERT(!backend_);
  backend_ = std::move(backend);
  backend_->set_portal(this);
}

IpczResult Portal::Close() {
  absl::MutexLock lock(&mutex_);
  return backend_->Close();
}

IpczResult Portal::QueryStatus(IpczPortalStatusFieldFlags field_flags,
                               IpczPortalStatus& status) {
  absl::MutexLock lock(&mutex_);
  return backend_->QueryStatus(field_flags, status);
}

IpczResult Portal::Put(absl::Span<const uint8_t> data,
                       absl::Span<const IpczHandle> ipcz_handles,
                       absl::Span<const IpczOSHandle> os_handles,
                       const IpczPutLimits* limits) {
  absl::MutexLock lock(&mutex_);
  return backend_->Put(data, ipcz_handles, os_handles, limits);
}

IpczResult Portal::BeginPut(uint32_t& num_data_bytes,
                            IpczBeginPutFlags flags,
                            const IpczPutLimits* limits,
                            void** data) {
  absl::MutexLock lock(&mutex_);
  return backend_->BeginPut(num_data_bytes, flags, limits, data);
}

IpczResult Portal::CommitPut(uint32_t num_data_bytes_produced,
                             absl::Span<const IpczHandle> ipcz_handles,
                             absl::Span<const IpczOSHandle> os_handles) {
  absl::MutexLock lock(&mutex_);
  return backend_->CommitPut(num_data_bytes_produced, ipcz_handles, os_handles);
}

IpczResult Portal::AbortPut() {
  absl::MutexLock lock(&mutex_);
  return backend_->AbortPut();
}

IpczResult Portal::Get(void* data,
                       uint32_t* num_data_bytes,
                       IpczHandle* ipcz_handles,
                       uint32_t* num_ipcz_handles,
                       IpczOSHandle* os_handles,
                       uint32_t* num_os_handles) {
  absl::MutexLock lock(&mutex_);
  return backend_->Get(data, num_data_bytes, ipcz_handles, num_ipcz_handles,
                       os_handles, num_os_handles);
}

IpczResult Portal::BeginGet(const void** data,
                            uint32_t* num_data_bytes,
                            IpczHandle* ipcz_handles,
                            uint32_t* num_ipcz_handles,
                            IpczOSHandle* os_handles,
                            uint32_t* num_os_handles) {
  absl::MutexLock lock(&mutex_);
  return backend_->BeginGet(data, num_data_bytes, ipcz_handles,
                            num_ipcz_handles, os_handles, num_os_handles);
}

IpczResult Portal::CommitGet(uint32_t num_data_bytes_consumed) {
  absl::MutexLock lock(&mutex_);
  return backend_->CommitGet(num_data_bytes_consumed);
}

IpczResult Portal::AbortGet() {
  absl::MutexLock lock(&mutex_);
  return backend_->AbortGet();
}

IpczResult Portal::CreateMonitor(const IpczMonitorDescriptor& descriptor,
                                 IpczHandle* handle) {
  absl::MutexLock lock(&mutex_);
  return backend_->CreateMonitor(descriptor, handle);
}

}  // namespace core
}  // namespace ipcz
