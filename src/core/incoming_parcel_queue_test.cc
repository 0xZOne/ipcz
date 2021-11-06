// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/incoming_parcel_queue.h"

#include "core/parcel.h"
#include "core/sequence_number.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/base/macros.h"

namespace ipcz {
namespace core {
namespace {

Parcel ParcelWithData(SequenceNumber n, size_t size) {
  Parcel p(n);
  p.SetData(std::vector<uint8_t>(size));
  return p;
}

TEST(IncomingParcelQueueTest, Empty) {
  IncomingParcelQueue q;
  EXPECT_TRUE(q.IsExpectingMoreParcels());
  EXPECT_FALSE(q.HasNextParcel());

  Parcel p;
  EXPECT_FALSE(q.Pop(p));
}

TEST(IncomingParcelQueueTest, SetPeerSequenceLength) {
  IncomingParcelQueue q;
  q.SetPeerSequenceLength(3);
  EXPECT_TRUE(q.IsExpectingMoreParcels());
  EXPECT_FALSE(q.HasNextParcel());

  Parcel p;
  EXPECT_FALSE(q.Pop(p));

  EXPECT_TRUE(q.Push(Parcel(2)));
  EXPECT_FALSE(q.HasNextParcel());
  EXPECT_FALSE(q.Pop(p));
  EXPECT_TRUE(q.IsExpectingMoreParcels());

  EXPECT_TRUE(q.Push(Parcel(0)));
  EXPECT_TRUE(q.HasNextParcel());
  EXPECT_TRUE(q.IsExpectingMoreParcels());
  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(0u, p.sequence_number());

  EXPECT_FALSE(q.HasNextParcel());
  EXPECT_FALSE(q.Pop(p));
  EXPECT_TRUE(q.IsExpectingMoreParcels());

  EXPECT_TRUE(q.Push(Parcel(1)));
  EXPECT_FALSE(q.IsExpectingMoreParcels());
  EXPECT_TRUE(q.HasNextParcel());
  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(1u, p.sequence_number());
  EXPECT_FALSE(q.IsExpectingMoreParcels());
  EXPECT_TRUE(q.HasNextParcel());
  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(2u, p.sequence_number());
  EXPECT_FALSE(q.IsExpectingMoreParcels());
  EXPECT_FALSE(q.HasNextParcel());
}

TEST(IncomingParcelQueueTest, SequenceTooLow) {
  IncomingParcelQueue q;

  Parcel p;
  EXPECT_TRUE(q.Push(Parcel(0)));
  EXPECT_TRUE(q.Pop(p));

  // We can't push another parcel with sequence number 0.
  EXPECT_FALSE(q.Push(Parcel(0)));

  // Out-of-order is of course fine.
  EXPECT_TRUE(q.Push(Parcel(2)));
  EXPECT_TRUE(q.Push(Parcel(1)));

  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(1u, p.sequence_number());
  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(2u, p.sequence_number());

  // But we can't revisit sequence number 1 or 2 either.
  EXPECT_FALSE(q.Push(Parcel(2)));
  EXPECT_FALSE(q.Push(Parcel(1)));
}

TEST(IncomingParcelQueueTest, SequenceTooHigh) {
  IncomingParcelQueue q;
  q.SetPeerSequenceLength(5);

  EXPECT_FALSE(q.Push(Parcel(5)));
}

TEST(IncomingParcelQueueTest, SparseSequence) {
  IncomingParcelQueue q;

  // Push a sparse but eventually complete sequence of messages into a queue and
  // ensure that they can only be popped out in sequence-order.
  SequenceNumber next_expected_pop = 0;
  SequenceNumber kMessageSequence[] = {5, 2, 1,  0,  4,  3,  9,  6,
                                       8, 7, 10, 11, 12, 15, 13, 14};
  for (SequenceNumber n : kMessageSequence) {
    EXPECT_TRUE(q.Push(Parcel(n)));
    Parcel p;
    while (q.Pop(p)) {
      EXPECT_EQ(next_expected_pop, p.sequence_number());
      ++next_expected_pop;
    }
  }

  EXPECT_EQ(16u, next_expected_pop);
}

TEST(IncomingParcelQueueTest, Accounting) {
  IncomingParcelQueue q;

  constexpr size_t kParcel0Size = 42;
  constexpr size_t kParcel1Size = 5;
  constexpr size_t kParcel2Size = 7;
  constexpr size_t kParcel3Size = 101;

  // Parcels not at the head of the queue are not considered to be available.
  EXPECT_TRUE(q.Push(ParcelWithData(3, kParcel3Size)));
  EXPECT_EQ(0u, q.GetNumAvailableParcels());
  EXPECT_EQ(0u, q.GetNumAvailableBytes());
  EXPECT_FALSE(q.HasNextParcel());

  EXPECT_TRUE(q.Push(ParcelWithData(1, kParcel1Size)));
  EXPECT_EQ(0u, q.GetNumAvailableParcels());
  EXPECT_EQ(0u, q.GetNumAvailableBytes());
  EXPECT_FALSE(q.HasNextParcel());

  // Now we'll insert at the head of the queue and we should be accounting for
  // parcels 0 and 1, but still not parcel 3 yet.
  EXPECT_TRUE(q.Push(ParcelWithData(0, kParcel0Size)));
  EXPECT_EQ(2u, q.GetNumAvailableParcels());
  EXPECT_EQ(kParcel0Size + kParcel1Size, q.GetNumAvailableBytes());
  EXPECT_TRUE(q.HasNextParcel());

  // Finally insert parcel 2, after which we should be accounting for all 4
  // parcels.
  EXPECT_TRUE(q.Push(ParcelWithData(2, kParcel2Size)));
  EXPECT_EQ(4u, q.GetNumAvailableParcels());
  EXPECT_EQ(kParcel0Size + kParcel1Size + kParcel2Size + kParcel3Size,
            q.GetNumAvailableBytes());

  // Popping should also update the accounting properly.
  Parcel p;
  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(0u, p.sequence_number());
  EXPECT_EQ(3u, q.GetNumAvailableParcels());
  EXPECT_EQ(kParcel1Size + kParcel2Size + kParcel3Size,
            q.GetNumAvailableBytes());

  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(1u, p.sequence_number());
  EXPECT_EQ(2u, q.GetNumAvailableParcels());
  EXPECT_EQ(kParcel2Size + kParcel3Size, q.GetNumAvailableBytes());

  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(2u, p.sequence_number());
  EXPECT_EQ(1u, q.GetNumAvailableParcels());
  EXPECT_EQ(kParcel3Size, q.GetNumAvailableBytes());

  EXPECT_TRUE(q.Pop(p));
  EXPECT_EQ(3u, p.sequence_number());
  EXPECT_EQ(0u, q.GetNumAvailableParcels());
  EXPECT_EQ(0u, q.GetNumAvailableBytes());
}

}  // namespace
}  // namespace core
}  // namespace ipcz