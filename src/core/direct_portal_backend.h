// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_CORE_DIRECT_PORTAL_BACKEND_H_
#define IPCZ_SRC_CORE_DIRECT_PORTAL_BACKEND_H_

#include <cstdint>
#include <utility>

#include "core/portal_backend.h"
#include "ipcz/ipcz.h"
#include "mem/ref_counted.h"

namespace ipcz {
namespace core {

class Node;
class Portal;

// PortalBackend implementation for a portal whose peer lives in the same node.
// This backend grants portals direct access to each others' state for more
// efficient operations with no dependency on Node state or routing behavior.
class DirectPortalBackend : public PortalBackend {
 public:
  using Pair = std::pair<std::unique_ptr<DirectPortalBackend>,
                         std::unique_ptr<DirectPortalBackend>>;

  ~DirectPortalBackend() override;

  static Pair CreatePair(mem::Ref<Node> node);

  // PortalBackend:
  IpczResult Close() override;
  IpczResult QueryStatus(IpczPortalStatusFieldFlags field_flags,
                         IpczPortalStatus& status) override;
  IpczResult Put(absl::Span<const uint8_t> data,
                 absl::Span<const IpczHandle> ipcz_handles,
                 absl::Span<const IpczOSHandle> os_handles,
                 const IpczPutLimits* limits) override;
  IpczResult BeginPut(uint32_t& num_data_bytes,
                      IpczBeginPutFlags flags,
                      const IpczPutLimits* limits,
                      void** data) override;
  IpczResult CommitPut(uint32_t num_data_bytes_produced,
                       absl::Span<const IpczHandle> ipcz_handles,
                       absl::Span<const IpczOSHandle> os_handles) override;
  IpczResult AbortPut() override;
  IpczResult Get(void* data,
                 uint32_t* num_data_bytes,
                 IpczHandle* ipcz_handles,
                 uint32_t* num_ipcz_handles,
                 IpczOSHandle* os_handles,
                 uint32_t* num_os_handles) override;
  IpczResult BeginGet(const void** data,
                      uint32_t* num_data_bytes,
                      IpczHandle* ipcz_handles,
                      uint32_t* num_ipcz_handles,
                      IpczOSHandle* os_handles,
                      uint32_t* num_os_handles) override;
  IpczResult CommitGet(uint32_t num_data_bytes_consumed) override;
  IpczResult AbortGet() override;
  IpczResult CreateMonitor(const IpczMonitorDescriptor& descriptor,
                           IpczHandle* handle) override;

 private:
  struct SharedState;
  struct PortalState;

  DirectPortalBackend(mem::Ref<SharedState> state, size_t side);

  PortalState& this_side();
  PortalState& other_side();

  const mem::Ref<SharedState> state_;
  const size_t side_;
};

}  // namespace core
}  // namespace ipcz

#endif  // IPCZ_SRC_CORE_DIRECT_PORTAL_BACKEND_H_
