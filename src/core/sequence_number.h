// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_CORE_SEQUENCE_NUMBER_H_
#define IPCZ_SRC_CORE_SEQUENCE_NUMBER_H_

#include <cstdint>

namespace ipcz {
namespace core {

// TODO: strong alias?
using SequenceNumber = uint64_t;

constexpr SequenceNumber kInvalidSequenceNumber =
    ~static_cast<SequenceNumber>(0);

}  // namespace core
}  // namespace ipcz

#endif  // IPCZ_SRC_CORE_SEQUENCE_NUMBER_H_