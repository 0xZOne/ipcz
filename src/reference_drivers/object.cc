// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reference_drivers/object.h"

#include "ipcz/ipcz.h"

namespace ipcz {
namespace reference_drivers {

Object::Object(Type type) : type_(type) {}

Object::~Object() = default;

IpczResult Object::Close() {
  return IPCZ_RESULT_OK;
}

}  // namespace reference_drivers
}  // namespace ipcz
