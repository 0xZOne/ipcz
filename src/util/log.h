// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_UTIL_LOG_H_
#define IPCZ_SRC_UTIL_LOG_H_

#if defined(IPCZ_STANDALONE)
#include "standalone/base/logging.h"
#else
#include "base/logging.h"
#endif

#endif  // IPCZ_SRC_UTIL_LOG_H_
