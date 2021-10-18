// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/routed_portal_backend.h"

#include <utility>

#include "core/node.h"

namespace ipcz {
namespace core {

RoutedPortalBackend::RoutedPortalBackend(mem::Ref<Node> node,
                                         const PortalName& name)
    : node_(std::move(node)), name_(name) {}

RoutedPortalBackend::~RoutedPortalBackend() = default;

IpczResult RoutedPortalBackend::Close() {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::QueryStatus(
    IpczPortalStatusFieldFlags field_flags,
    IpczPortalStatus& status) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::Put(absl::Span<const uint8_t> data,
                                    absl::Span<const IpczHandle> portals,
                                    absl::Span<const IpczOSHandle> os_handles,
                                    const IpczPutLimits* limits) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::BeginPut(IpczBeginPutFlags flags,
                                         const IpczPutLimits* limits,
                                         uint32_t& num_data_bytes,
                                         void** data) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::CommitPut(
    uint32_t num_data_bytes_produced,
    absl::Span<const IpczHandle> portals,
    absl::Span<const IpczOSHandle> os_handles) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::AbortPut() {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::Get(void* data,
                                    uint32_t* num_data_bytes,
                                    IpczHandle* portals,
                                    uint32_t* num_portals,
                                    IpczOSHandle* os_handles,
                                    uint32_t* num_os_handles) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::BeginGet(const void** data,
                                         uint32_t* num_data_bytes,
                                         uint32_t* num_portals,
                                         uint32_t* num_os_handles) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::CommitGet(uint32_t num_data_bytes_consumed,
                                          IpczHandle* portals,
                                          uint32_t* num_portals,
                                          IpczOSHandle* os_handles,
                                          uint32_t* num_os_handles) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult RoutedPortalBackend::AbortGet() {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

}  // namespace core
}  // namespace ipcz