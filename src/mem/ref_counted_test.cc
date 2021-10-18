// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mem/ref_counted.h"

#include <atomic>
#include <thread>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/synchronization/notification.h"

namespace ipcz {
namespace mem {
namespace {

using RefCountedTest = testing::Test;

class TestObject : public RefCounted {
 public:
  explicit TestObject(bool& destruction_flag)
      : destruction_flag_(destruction_flag) {}

  size_t count() const { return count_.load(std::memory_order_acquire); }

  void Increment() { count_.fetch_add(1, std::memory_order_relaxed); }

 private:
  ~TestObject() override { destruction_flag_ = true; }

  bool& destruction_flag_;
  std::atomic<size_t> count_{0};
};

TEST_F(RefCountedTest, NullRef) {
  Ref<TestObject> ref;
  EXPECT_EQ(nullptr, ref);
  EXPECT_EQ(nullptr, ref.get());

  ref.reset();
  EXPECT_EQ(nullptr, ref);
  EXPECT_EQ(nullptr, ref.get());

  Ref<TestObject> other1 = ref;
  EXPECT_EQ(nullptr, ref);
  EXPECT_EQ(nullptr, ref.get());
  EXPECT_EQ(nullptr, other1);
  EXPECT_EQ(nullptr, other1.get());

  Ref<TestObject> other2 = std::move(ref);
  EXPECT_EQ(nullptr, ref);
  EXPECT_EQ(nullptr, ref.get());
  EXPECT_EQ(nullptr, other2);
  EXPECT_EQ(nullptr, other2.get());

  ref = other1;
  EXPECT_EQ(nullptr, ref);
  EXPECT_EQ(nullptr, ref.get());
  EXPECT_EQ(nullptr, other1);
  EXPECT_EQ(nullptr, other1.get());

  ref = std::move(other2);
  EXPECT_EQ(nullptr, ref);
  EXPECT_EQ(nullptr, ref.get());
  EXPECT_EQ(nullptr, other2);
  EXPECT_EQ(nullptr, other2.get());
}

TEST_F(RefCountedTest, SimpleRef) {
  bool destroyed = false;
  auto ref = mem::MakeRefCounted<TestObject>(destroyed);
  EXPECT_FALSE(destroyed);
  ref.reset();
  EXPECT_EQ(nullptr, ref);
  EXPECT_TRUE(destroyed);
}

TEST_F(RefCountedTest, Copy) {
  bool destroyed1 = false;
  auto ref1 = mem::MakeRefCounted<TestObject>(destroyed1);
  Ref<TestObject> other1 = ref1;
  EXPECT_FALSE(destroyed1);
  ref1.reset();
  EXPECT_EQ(nullptr, ref1);
  EXPECT_FALSE(destroyed1);
  other1.reset();
  EXPECT_EQ(nullptr, other1);
  EXPECT_TRUE(destroyed1);

  destroyed1 = false;
  bool destroyed2 = false;
  ref1 = mem::MakeRefCounted<TestObject>(destroyed1);
  auto ref2 = mem::MakeRefCounted<TestObject>(destroyed2);
  EXPECT_FALSE(destroyed1);
  EXPECT_FALSE(destroyed2);
  ref2 = ref1;
  EXPECT_NE(nullptr, ref1);
  EXPECT_NE(nullptr, ref2);
  EXPECT_EQ(ref1, ref2);
  EXPECT_FALSE(destroyed1);
  EXPECT_TRUE(destroyed2);
  ref1.reset();
  EXPECT_EQ(nullptr, ref1);
  EXPECT_FALSE(destroyed1);
  EXPECT_TRUE(destroyed2);
  ref2.reset();
  EXPECT_EQ(nullptr, ref2);
  EXPECT_TRUE(destroyed1);
}

TEST_F(RefCountedTest, Move) {
  bool destroyed1 = false;
  auto ref1 = mem::MakeRefCounted<TestObject>(destroyed1);
  Ref<TestObject> other1 = std::move(ref1);
  EXPECT_EQ(nullptr, ref1);
  EXPECT_FALSE(destroyed1);
  other1.reset();
  EXPECT_TRUE(destroyed1);

  destroyed1 = false;
  bool destroyed2 = false;
  ref1 = mem::MakeRefCounted<TestObject>(destroyed1);
  auto ref2 = mem::MakeRefCounted<TestObject>(destroyed2);
  EXPECT_FALSE(destroyed1);
  EXPECT_FALSE(destroyed2);
  ref2 = std::move(ref1);
  EXPECT_EQ(nullptr, ref1);
  EXPECT_NE(nullptr, ref2);
  EXPECT_FALSE(destroyed1);
  EXPECT_TRUE(destroyed2);
  ref2.reset();
  EXPECT_TRUE(destroyed1);
}

TEST_F(RefCountedTest, ThreadSafe) {
  bool destroyed = false;
  auto counter = mem::MakeRefCounted<TestObject>(destroyed);

  constexpr size_t kIncrementsPerThread = 10000;
  constexpr size_t kNumThreads = 64;
  auto incrementer = [](mem::Ref<TestObject> ref) {
    for (size_t i = 0; i < kIncrementsPerThread; ++i) {
      mem::Ref<TestObject> copy = ref;
      copy->Increment();
    }
  };

  std::vector<std::thread> threads;
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(incrementer, counter);
  }

  for (std::thread& thread : threads) {
    thread.join();
  }

  EXPECT_FALSE(destroyed);
  EXPECT_EQ(kNumThreads * kIncrementsPerThread, counter->count());
  counter.reset();
  EXPECT_TRUE(destroyed);
}

}  // namespace
}  // namespace mem
}  // namespace ipcz