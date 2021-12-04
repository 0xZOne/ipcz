// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/parcel.h"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

#include "core/portal.h"
#include "third_party/abseil-cpp/absl/types/span.h"
#include "util/handle_util.h"

namespace ipcz {
namespace core {

Parcel::Parcel() = default;

Parcel::Parcel(SequenceNumber sequence_number)
    : sequence_number_(sequence_number) {}

Parcel::Parcel(Parcel&& other) = default;

Parcel& Parcel::operator=(Parcel&& other) = default;

Parcel::~Parcel() {
  for (mem::Ref<Portal>& portal : portals_) {
    if (portal) {
      portal->Close();
    }
  }
}

void Parcel::SetData(std::vector<uint8_t> data) {
  data_ = std::move(data);
  data_view_ = absl::MakeSpan(data_);
}

void Parcel::SetPortals(PortalVector portals) {
  portals_ = std::move(portals);
}

void Parcel::SetOSHandles(std::vector<os::Handle> os_handles) {
  os_handles_ = std::move(os_handles);
}

void Parcel::ResizeData(size_t size) {
  data_.resize(size);
  data_view_ = absl::MakeSpan(data_);
}

void Parcel::Consume(IpczHandle* portals, IpczOSHandle* os_handles) {
  ConsumePortalsAndHandles(portals, os_handles);
  data_view_ = {};
}

void Parcel::ConsumePartial(size_t num_bytes_consumed,
                            IpczHandle* portals,
                            IpczOSHandle* os_handles) {
  data_view_ = data_view_.subspan(num_bytes_consumed);
  ConsumePortalsAndHandles(portals, os_handles);
}

void Parcel::ConsumePortalsAndHandles(IpczHandle* portals,
                                      IpczOSHandle* os_handles) {
  for (size_t i = 0; i < portals_.size(); ++i) {
    portals[i] = ToHandle(portals_[i].release());
  }
  for (size_t i = 0; i < os_handles_.size(); ++i) {
    os::Handle::ToIpczOSHandle(std::move(os_handles_[i]), &os_handles[i]);
  }
  portals_.clear();
  os_handles_.clear();
}

Parcel::PortalVector Parcel::TakePortals() {
  return std::move(portals_);
}

std::string Parcel::Describe() const {
  std::stringstream ss;
  ss << "parcel " << sequence_number() << " (";
  if (!data_view().empty()) {
    // Cheesy heuristic: if the first character is an ASCII letter or number,
    // assume the parcel data is human-readable and print a few characters.
    if (std::isalnum(data_view()[0])) {
      const absl::Span<const uint8_t> preview = data_view().subspan(0, 8);
      ss << "\"" << std::string(preview.begin(), preview.end());
      if (preview.size() < data_view().size()) {
        ss << "...\", " << data_view().size() << " bytes";
      } else {
        ss << '"';
      }
    }
  } else {
    ss << "no data";
  }
  if (!portals_view().empty()) {
    ss << ", " << portals_view().size() << " portals";
  }
  if (!os_handles_view().empty()) {
    ss << ", " << os_handles_view().size() << " handles";
  }
  ss << ")";
  return ss.str();
}

}  // namespace core
}  // namespace ipcz
