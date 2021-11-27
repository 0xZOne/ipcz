// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/portal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple>
#include <utility>

#include "core/local_router_link.h"
#include "core/node.h"
#include "core/parcel.h"
#include "core/portal_descriptor.h"
#include "core/router.h"
#include "core/side.h"
#include "core/trap.h"
#include "ipcz/ipcz.h"
#include "mem/ref_counted.h"
#include "os/handle.h"
#include "third_party/abseil-cpp/absl/base/macros.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"
#include "third_party/abseil-cpp/absl/types/span.h"
#include "util/handle_util.h"

namespace ipcz {
namespace core {

namespace {

bool ValidateAndAcquirePortalsForTransitFrom(
    Portal& sender,
    absl::Span<const IpczHandle> handles,
    Parcel::PortalVector& portals) {
  portals.resize(handles.size());
  for (size_t i = 0; i < handles.size(); ++i) {
    auto portal = mem::WrapRefCounted(ToPtr<Portal>(handles[i]));
    if (&sender == portal.get() ||
        sender.router()->HasLocalPeer(portal->router())) {
      return false;
    }
    portals[i] = std::move(portal);
  }
  return true;
}

}  // namespace

Portal::Portal(mem::Ref<Node> node, mem::Ref<Router> router)
    : node_(std::move(node)), router_(std::move(router)) {}

Portal::~Portal() = default;

// static
std::pair<mem::Ref<Portal>, mem::Ref<Portal>> Portal::CreatePair(
    mem::Ref<Node> node) {
  auto left = mem::MakeRefCounted<Portal>(
      node, mem::MakeRefCounted<Router>(Side::kLeft));
  auto right = mem::MakeRefCounted<Portal>(
      std::move(node), mem::MakeRefCounted<Router>(Side::kRight));
  mem::Ref<LocalRouterLink> left_link;
  mem::Ref<LocalRouterLink> right_link;
  std::tie(left_link, right_link) =
      LocalRouterLink::CreatePair(left->router(), right->router());
  left->router()->SetPeer(std::move(left_link));
  right->router()->SetPeer(std::move(right_link));
  return {std::move(left), std::move(right)};
}

IpczResult Portal::Close() {
  router_->CloseRoute();
  return IPCZ_RESULT_OK;
}

IpczResult Portal::QueryStatus(IpczPortalStatus& status) {
  router_->QueryStatus(status);
  return IPCZ_RESULT_OK;
}

IpczResult Portal::Put(absl::Span<const uint8_t> data,
                       absl::Span<const IpczHandle> portal_handles,
                       absl::Span<const IpczOSHandle> os_handles,
                       const IpczPutLimits* limits) {
  Parcel::PortalVector portals;
  if (!ValidateAndAcquirePortalsForTransitFrom(*this, portal_handles,
                                               portals)) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  if (limits &&
      router_->WouldOutgoingParcelExceedLimits(data.size(), *limits)) {
    return IPCZ_RESULT_RESOURCE_EXHAUSTED;
  }

  if (router_->IsPeerClosed()) {
    return IPCZ_RESULT_NOT_FOUND;
  }

  std::vector<os::Handle> handles(os_handles.size());
  for (size_t i = 0; i < os_handles.size(); ++i) {
    handles[i] = os::Handle::FromIpczOSHandle(os_handles[i]);
  }

  IpczResult result = router_->SendOutgoingParcel(data, portals, handles);
  if (result != IPCZ_RESULT_OK) {
    for (os::Handle& handle : handles) {
      (void)handle.release();
    }
    return result;
  }

  return IPCZ_RESULT_OK;
}

IpczResult Portal::BeginPut(IpczBeginPutFlags flags,
                            const IpczPutLimits* limits,
                            uint32_t& num_data_bytes,
                            void** data) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult Portal::CommitPut(uint32_t num_data_bytes_produced,
                             absl::Span<const IpczHandle> portals,
                             absl::Span<const IpczOSHandle> os_handles) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult Portal::AbortPut() {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult Portal::Get(void* data,
                       uint32_t* num_data_bytes,
                       IpczHandle* portals,
                       uint32_t* num_portals,
                       IpczOSHandle* os_handles,
                       uint32_t* num_os_handles) {
  return router_->GetNextIncomingParcel(
      data, num_data_bytes, portals, num_portals, os_handles, num_os_handles);
}

IpczResult Portal::BeginGet(const void** data,
                            uint32_t* num_data_bytes,
                            uint32_t* num_portals,
                            uint32_t* num_os_handles) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult Portal::CommitGet(uint32_t num_data_bytes_consumed,
                             IpczHandle* portals,
                             uint32_t* num_portals,
                             IpczOSHandle* os_handles,
                             uint32_t* num_os_handles) {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult Portal::AbortGet() {
  return IPCZ_RESULT_UNIMPLEMENTED;
}

IpczResult Portal::CreateTrap(const IpczTrapConditions& conditions,
                              IpczTrapEventHandler handler,
                              uintptr_t context,
                              IpczHandle& trap) {
  auto new_trap = std::make_unique<Trap>(conditions, handler, context);
  trap = ToHandle(new_trap.get());
  return router_->AddTrap(std::move(new_trap));
}

IpczResult Portal::ArmTrap(IpczHandle trap,
                           IpczTrapConditionFlags* satisfied_condition_flags,
                           IpczPortalStatus* status) {
  IpczTrapConditionFlags flags = 0;
  IpczResult result = router_->ArmTrap(ToRef<Trap>(trap), flags, status);
  if (result == IPCZ_RESULT_OK) {
    return IPCZ_RESULT_OK;
  }

  if (satisfied_condition_flags) {
    *satisfied_condition_flags = flags;
  }
  return result;
}

IpczResult Portal::DestroyTrap(IpczHandle trap) {
  return router_->RemoveTrap(ToRef<Trap>(trap));
}

}  // namespace core
}  // namespace ipcz
