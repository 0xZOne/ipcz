// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipcz/ipcz.h"
#include "test/api_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ipcz {
namespace {

using QueryStatusAPITest = test::APITest;

TEST_F(QueryStatusAPITest, QueryClosedBit) {
  IpczHandle a, b;
  EXPECT_EQ(IPCZ_RESULT_OK,
            ipcz.OpenPortals(node(), IPCZ_NO_FLAGS, nullptr, &a, &b));

  IpczPortalStatus status = {sizeof(status)};
  EXPECT_EQ(IPCZ_RESULT_OK,
            ipcz.QueryPortalStatus(a, IPCZ_PORTAL_STATUS_FIELD_BITS,
                                   IPCZ_NO_FLAGS, nullptr, &status));
  EXPECT_EQ(IPCZ_NO_FLAGS, status.bits);
  EXPECT_EQ(IPCZ_RESULT_OK, ipcz.ClosePortal(b, IPCZ_NO_FLAGS, nullptr));
  EXPECT_EQ(IPCZ_RESULT_OK,
            ipcz.QueryPortalStatus(a, IPCZ_PORTAL_STATUS_FIELD_BITS,
                                   IPCZ_NO_FLAGS, nullptr, &status));
  EXPECT_EQ(IPCZ_PORTAL_STATUS_BIT_CLOSED, status.bits);

  EXPECT_EQ(IPCZ_RESULT_OK, ipcz.ClosePortal(a, IPCZ_NO_FLAGS, nullptr));
}

}  // namespace
}  // namespace ipcz
