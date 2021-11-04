// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/portal_link.h"

#include <utility>

#include "core/node_link.h"

namespace ipcz {
namespace core {

PortalLink::PortalLink(mem::Ref<NodeLink> node,
                       RouteId route,
                       os::Memory::Mapping control_block)
    : node_(std::move(node)),
      route_(route),
      control_block_(std::move(control_block)) {}

PortalLink::~PortalLink() = default;

void PortalLink::SendParcel(Parcel& parcel) {
  node_->SendParcel(route_, parcel);
}

void PortalLink::NotifyClosed() {
  node_->SendPeerClosed(route_);
}

}  // namespace core
}  // namespace ipcz
