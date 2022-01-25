// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_CORE_SUBLINK_ID_H_
#define IPCZ_SRC_CORE_SUBLINK_ID_H_

#include <cstdint>

namespace ipcz {
namespace core {

// Identifies a specific subsidiary link along a NodeLink. Each sublink is a
// path between a unique pair of Router instances, one on each linked node. New
// SublinkIds are allocated atomically by either side of the NodeLink.
//
// TODO: strong alias?
using SublinkId = uint64_t;

}  // namespace core
}  // namespace ipcz

#endif  // IPCZ_SRC_CORE_SUBLINK_ID_H_
