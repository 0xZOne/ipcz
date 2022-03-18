// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_IPCZ_SEQUENCED_QUEUE_H_
#define IPCZ_SRC_IPCZ_SEQUENCED_QUEUE_H_

#include <cstddef>
#include <vector>

#include "ipcz/sequence_number.h"
#include "third_party/abseil-cpp/absl/container/inlined_vector.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace ipcz {

template <typename T>
struct DefaultSequencedQueueTraits {
  static size_t GetElementSize(const T& element) { return 0; }
};

// SequencedQueue retains a queue of objects strictly ordered by SequenceNumber.
//
// This is useful in situations where queued elements may accumulate slightly
// out-of-order and need to be reordered efficiently for consumption. The
// implementation relies on an assumption that sequence gaps are common but tend
// to be small and short-lived. As such, a SequencedQueue retains at least
// enough linear storage to hold every object between the last popped
// SequenceNumber (exclusive) and the highest queued (or antiticapted)
// SequenceNumber so far (inclusive).
//
// Storage may be sparsely populated at times, but as elements are consumed from
// the queue, storage is compacted to reduce waste.
template <typename T, typename ElementTraits = DefaultSequencedQueueTraits<T>>
class SequencedQueue {
 public:
  SequencedQueue() = default;

  explicit SequencedQueue(SequenceNumber initial_sequence_number)
      : base_sequence_number_(initial_sequence_number) {}

  SequencedQueue(SequencedQueue&& other)
      : base_sequence_number_(other.base_sequence_number_),
        num_entries_(other.num_entries_),
        final_sequence_length_(other.final_sequence_length_) {
    if (!other.storage_.empty()) {
      size_t entries_offset = other.entries_.data() - storage_.data();
      storage_ = std::move(other.storage_);
      entries_ =
          EntryView(storage_.data() + entries_offset, other.entries_.size());
    }
  }

  SequencedQueue& operator=(SequencedQueue&& other) {
    base_sequence_number_ = other.base_sequence_number_;
    num_entries_ = other.num_entries_;
    final_sequence_length_ = other.final_sequence_length_;
    if (!other.storage_.empty()) {
      size_t entries_offset = other.entries_.data() - storage_.data();
      storage_ = std::move(other.storage_);
      entries_ =
          EntryView(storage_.data() + entries_offset, other.entries_.size());
    } else {
      storage_.clear();
      entries_ = EntryView(storage_.data(), 0);
    }
    return *this;
  }

  ~SequencedQueue() = default;

  static constexpr SequenceNumber GetMaxSequenceGap() { return 1000000; }

  // The SequenceNumber of the next element that is or will be available from
  // the queue. This starts at the constructor's `initial_sequence_number` and
  // increments any time an element is successfully popped from the queue.
  SequenceNumber current_sequence_number() const {
    return base_sequence_number_;
  }

  // The final length of the sequence to be popped from this queue. Null if the
  // final length is not yet known. If this is N, then the last ordered element
  // that can be pushed to or popped from the queue has a SequenceNumber of N-1.
  const absl::optional<SequenceNumber>& final_sequence_length() const {
    return final_sequence_length_;
  }

  // Returns the number of elements currently ready for popping at the front of
  // the queue. This is the number of contiguously sequenced elements
  // available starting from `current_sequence_number()`. If the current
  // SequenceNumber is 5 and this queue holds elements 5, 6, and 8, then this
  // method returns 2: only elements 5 and 6 are available.
  size_t GetNumAvailableElements() const {
    if (entries_.empty() || !entries_[0].has_value()) {
      return 0;
    }

    return entries_[0]->num_entries_in_span;
  }

  // Returns the total "size" of elements currently ready for popping at the
  // front of the queue. This is the sum of ElementTraits::GetElementSize() for
  // each entry counted by `GetNumAvailableElements()` and is returned in
  // constant time.
  size_t GetTotalAvailableElementSize() const {
    if (entries_.empty() || !entries_[0].has_value()) {
      return 0;
    }

    return entries_[0]->total_span_size;
  }

  // Returns the length of the sequence known so far by this queue. This is
  // essentially `current_sequence_number()` plus `GetNumAvailableElements()`.
  // For example if `current_sequence_number()` is 5 and
  // `GetNumAvailableElements()` is 3, then elements 5, 6, and 7 are available
  // for retrieval and the total length of the sequence so far is 8; so this
  // method would return 8.
  SequenceNumber GetCurrentSequenceLength() const {
    return current_sequence_number() + GetNumAvailableElements();
  }

  // Sets the known final length of the incoming sequence. This is the sequence
  // number of the last element that can be pushed, plus 1; or 0 if no elements
  // can be pushed.
  //
  // May fail and return false if the queue already has elements with a sequence
  // number greater than or equal to `length`, or if a the final sequence length
  // had already been set prior to this call.
  bool SetFinalSequenceLength(SequenceNumber length) {
    if (final_sequence_length_) {
      return false;
    }

    if (length < base_sequence_number_ + entries_.size()) {
      return false;
    }

    if (length - base_sequence_number_ > GetMaxSequenceGap()) {
      return false;
    }

    final_sequence_length_ = length;
    return Reallocate(length);
  }

  // Indicates whether this queue is still waiting to have more elements pushed.
  // This is always true if the final sequence length has not been set yet. Once
  // the final sequence length is set, this remains true only until all elements
  // between the initial sequence number (inclusive) and the final sequence
  // length (exclusive) have been pushed into (and optionally popped from) the
  // queue.
  bool ExpectsMoreElements() const {
    if (!final_sequence_length_) {
      return true;
    }

    if (base_sequence_number_ >= *final_sequence_length_) {
      return false;
    }

    const size_t num_entries_remaining =
        *final_sequence_length_ - base_sequence_number_;
    return num_entries_ < num_entries_remaining;
  }

  // Indicates whether the next element (in sequence order) is available to pop.
  bool HasNextElement() const {
    return !entries_.empty() && entries_[0].has_value();
  }

  // Indicates if there are no elements in this queue, not even ones beyond the
  // current sequence number that are merely unavailable.
  bool IsEmpty() const { return num_entries_ == 0; }

  // Indicates whether this queue is "dead," meaning it will no longer accept
  // new elements AND there are no more elements left pop. This occurs iff the
  // final sequence length is known, and all elements from the initial sequence
  // number up to the final sequence length have been pushed into and then
  // popped from this queue.
  bool IsDead() const { return !HasNextElement() && !ExpectsMoreElements(); }

  // Resets this queue to start at the initial SequenceNumber `n`. Must be
  // called only on an empty queue (IsEmpty() == true) and only when the caller
  // can be sure they won't want to push any elements with a SequenceNumber
  // below `n`.
  void ResetInitialSequenceNumber(SequenceNumber n) {
    ABSL_ASSERT(IsEmpty());
    base_sequence_number_ = n;
  }

  // Skips the next SequenceNumber by advancing `base_sequence_number_` by one.
  // Must be called only when no elements are currently available in the queue.
  void SkipNextSequenceNumber() {
    ABSL_ASSERT(!HasNextElement());
    ++base_sequence_number_;
    if (!IsEmpty()) {
      entries_.remove_prefix(1);
    }
  }

  // Pushes an element into the queue with the given SequenceNumber. This may
  // fail if `n` falls below the minimum or maximum (when applicable) expected
  // sequence number for elements in this queue.
  bool Push(SequenceNumber n, T element) {
    if (n < base_sequence_number_ ||
        (n - base_sequence_number_ > GetMaxSequenceGap())) {
      return false;
    }

    size_t index = n - base_sequence_number_;
    if (final_sequence_length_) {
      if (index >= entries_.size() || entries_[index].has_value()) {
        return false;
      }
      PlaceNewEntry(index, n, element);
      return true;
    }

    if (index < entries_.size()) {
      if (entries_[index].has_value()) {
        return false;
      }
      PlaceNewEntry(index, n, element);
      return true;
    }

    SequenceNumber new_limit = n + 1;
    if (new_limit == 0) {
      // TODO: Gracefully handle overflow / wraparound?
      return false;
    }

    if (!Reallocate(new_limit)) {
      return false;
    }

    PlaceNewEntry(index, n, element);
    return true;
  }

  // Pops the next (in sequence order) element off the queue if available,
  // populating `element` with its contents and returning true on success. On
  // failure `element` is untouched and this returns false.
  bool Pop(T& element) {
    if (entries_.empty() || !entries_[0].has_value()) {
      return false;
    }

    Entry& head = *entries_[0];
    element = std::move(head.element);

    ABSL_ASSERT(num_entries_ > 0);
    --num_entries_;
    const SequenceNumber sequence_number = base_sequence_number_++;

    // Make sure the next queued entry has up-to-date accounting, if present.
    if (entries_.size() > 1 && entries_[1]) {
      Entry& next = *entries_[1];
      next.span_start = head.span_start;
      next.span_end = head.span_end;
      next.num_entries_in_span = head.num_entries_in_span - 1;
      next.total_span_size =
          head.total_span_size - ElementTraits::GetElementSize(element);

      size_t tail_index = next.span_end - sequence_number;
      if (tail_index > 1) {
        Entry& tail = *entries_[tail_index];
        tail.num_entries_in_span = next.num_entries_in_span;
        tail.total_span_size = next.total_span_size;
      }
    }

    entries_[0].reset();
    entries_ = entries_.subspan(1);

    // If there's definitely no more populated element data, take this
    // opportunity to realign `entries_` to the front of `storage_` to reduce
    // future allocations.
    if (num_entries_ == 0) {
      entries_ = EntryView(storage_.data(), entries_.size());
    }

    return true;
  }

  // Gets a reference to the next element. This reference is NOT stable across
  // any non-const methods here.
  T& NextElement() {
    ABSL_ASSERT(HasNextElement());
    return entries_[0]->element;
  }

 protected:
  void ReduceNextElementSize(size_t amount) {
    ABSL_ASSERT(HasNextElement());
    ABSL_ASSERT(entries_[0]->total_span_size >= amount);
    entries_[0]->total_span_size -= amount;
  }

 private:
  bool Reallocate(SequenceNumber sequence_length) {
    if (sequence_length < base_sequence_number_) {
      return false;
    }

    size_t new_entries_size = sequence_length - base_sequence_number_;
    if (new_entries_size > GetMaxSequenceGap()) {
      return false;
    }

    size_t entries_offset = entries_.data() - storage_.data();
    if (storage_.size() - entries_offset > new_entries_size) {
      // Fast path: just extend the view into storage.
      entries_ = EntryView(storage_.data() + entries_offset, new_entries_size);
      return true;
    }

    // We need to reallocate storage. Re-align `entries_` with the front of the
    // buffer, and leave some extra room when allocating.
    if (entries_offset > 0) {
      for (size_t i = 0; i < entries_.size(); ++i) {
        storage_[i] = std::move(entries_[i]);
        entries_[i].reset();
      }
    }

    storage_.resize(new_entries_size * 2);
    entries_ = EntryView(storage_.data(), new_entries_size);
    return true;
  }

  void PlaceNewEntry(size_t index, SequenceNumber n, T& element) {
    ABSL_ASSERT(index < entries_.size());
    ABSL_ASSERT(!entries_[index].has_value());

    entries_[index].emplace();
    Entry& entry = *entries_[index];
    entry.num_entries_in_span = 1;
    entry.total_span_size = ElementTraits::GetElementSize(element);

    entry.element = std::move(element);

    if (index == 0 || !entries_[index - 1]) {
      entry.span_start = n;
    } else {
      Entry& left = *entries_[index - 1];
      entry.span_start = left.span_start;
      entry.num_entries_in_span += left.num_entries_in_span;
      entry.total_span_size += left.total_span_size;
    }

    if (index == entries_.size() - 1 || !entries_[index + 1]) {
      entry.span_end = n;
    } else {
      Entry& right = *entries_[index + 1];
      entry.span_end = right.span_end;
      entry.num_entries_in_span += right.num_entries_in_span;
      entry.total_span_size += right.total_span_size;
    }

    Entry* start;
    if (entry.span_start <= base_sequence_number_) {
      start = &entries_[0].value();
    } else {
      start = &entries_[entry.span_start - base_sequence_number_].value();
    }

    ABSL_ASSERT(entry.span_end >= base_sequence_number_);
    size_t end_index = entry.span_end - base_sequence_number_;
    ABSL_ASSERT(end_index < entries_.size());
    Entry* end = &entries_[end_index].value();

    start->span_end = entry.span_end;
    start->num_entries_in_span = entry.num_entries_in_span;
    start->total_span_size = entry.total_span_size;

    end->span_start = entry.span_start;
    end->num_entries_in_span = entry.num_entries_in_span;
    end->total_span_size = entry.total_span_size;

    ++num_entries_;
  }

  struct Entry {
    Entry() = default;
    Entry(Entry&& other) = default;
    Entry& operator=(Entry&&) = default;
    ~Entry() = default;

    T element;

    // NOTE: The fields below are maintained during Push and Pop operations and
    // are used to support efficient implementation of GetNumAvailableElements()
    // and GetTotalAvailableSize(). This warrants some clarification.
    //
    // Conceptually we treat the active range of entries as a series of
    // contiguous spans:
    //
    //     `entries_`: [2][ ][4][5][6][ ][8][9]
    //
    // For example, above we can designate three contiguous spans: element 2
    // stands alone at the front of the queue, element 4-6 form a second span,
    // and then elements 8-9 form the third. Elements 3 and 7 are absent.
    //
    // We're interested in knowing how many elements (and their total size in
    // bytes) are available right now, which means we want to answer the
    // question: how long is the span starting at element 0? In this case since
    // element 2 stands alone at the front of the queue, the answer is 1.
    // There's 1 element available right now.
    //
    // If we pop element 2 off the queue, it then becomes:
    //
    //     `entries_`: [ ][4][5][6][ ][8][9]
    //
    // The head of the queue is pointing at the empty slot for element 3, and
    // because no span starts in element 0 there are now 0 elements available to
    // pop.
    //
    // Finally if we then push element 3, the queue looks like this:
    //
    //     `entries_`: [3][4][5][6][ ][8][9]
    //
    // and now there are 4 elements available to pop. Element 0 begins the span
    // of elements 3, 4, 5, and 6.
    //
    // To answer the question efficiently though, each entry records some
    // metadata about the span in which it resides. This information is not kept
    // up-to-date for all entries, but we maintain the invariant that the first
    // and last element of each distinct span has accurate metadata; and as a
    // consequence if any span starts at element 0, then we know element 0's
    // metadata accurately answers our general questions about the queue.
    //
    // When an element with sequence number N is inserted into the queue, it can
    // be classified in one of four ways:
    //
    //    (1) it stands alone with no element at N-1 or N+1
    //    (2) it follows a element at N-1, but N+1 is empty
    //    (3) it precedes a element at N+1, but N-1 is empty
    //    (4) it falls between a element at N-1 and a element at N+1.
    //
    // In case (1) we record in the entry that its span starts and ends at
    // element N; we also record the length of the span (1) and atraits-defined
    // accounting of the element's "size". This entry now has trivially correct
    // metadata about its containing span, of which it is both the head and
    // tail.
    //
    // In case (2), element N is now the tail of a pre-existing span. Because
    // tail elements are always up-to-date, we simply copy and augment the data
    // from the old tail (element N-1) into the new tail (element N). From this
    // data we also know where the head of the span is, so we can update it with
    // the same new metadata.
    //
    // Case (3) is similar to case (2). Element N is now the head of a
    // pre-existing span, so we copy and augment the already up-to-date N+1
    // entry's metadata (the old head) into our new entry as well as the span's
    // tail entry.
    //
    // Case (4) is joining two pre-existing spans. In this case element N
    // fetches the span's start from element N-1 (the tail of the span to the
    // left), and the span's end from element N+1 (the head of the span to the
    // right); and it sums their element and byte counts with its own. This new
    // combined metadata is copied into both the head of the left span and the
    // tail of the right span, and with element N populated this now constitutes
    // a single combined span with accurate metadata in its head and tail
    // entries.
    //
    // Finally, the only other operation that matters for this accounting is
    // Pop(). All Pop() needs to do though is derive new metadata for the new
    // head-of-queue's span (if present) after popping. This metadata will
    // update both the new head-of-queue as well as its span's tail.
    size_t num_entries_in_span = 0;
    size_t total_span_size = 0;
    SequenceNumber span_start = 0;
    SequenceNumber span_end = 0;
  };

  using EntryStorage = absl::InlinedVector<absl::optional<Entry>, 4>;
  using EntryView = absl::Span<absl::optional<Entry>>;

  // This is a sparse vector of queued elements indexed by a relative sequence
  // number.
  //
  // It's sparse because the queue may push elements out of sequence order (e.g.
  // elements 42 and 47 may be pushed before elements 43-46.)
  EntryStorage storage_;

  // A view into `storage_` whose first element corresponds to the entry with
  // sequence number `base_sequence_number_`. As elements are popped, the view
  // moves forward in `storage_`. When convenient, we may reallocate `storage_`
  // and realign this view.
  EntryView entries_{storage_.data(), 0};

  // The sequence number which corresponds to `entries_` index 0 when `entries_`
  // is non-empty.
  SequenceNumber base_sequence_number_ = 0;

  // The number of slots in `entries_` which are actually occupied.
  size_t num_entries_ = 0;

  // The known final length of the sequence to be enqueued, if known.
  absl::optional<SequenceNumber> final_sequence_length_;
};

}  // namespace ipcz

#endif  // IPCZ_SRC_IPCZ_SEQUENCED_QUEUE_H_
