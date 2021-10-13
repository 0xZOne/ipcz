// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_OS_HANDLE_H_
#define IPCZ_SRC_OS_HANDLE_H_

#include <algorithm>

#include "build/build_config.h"
#include "ipcz/ipcz.h"

#if defined(OS_WIN)
#include <windows.h>
#elif defined(OS_FUCHSIA)
#include <lib/zx/handle.h>
#elif defined(OS_MAC)
#include <mach/mach.h>
#endif

namespace ipcz {
namespace os {

// Generic scoper to wrap various types of platform-specific OS handles.
// Depending on target platform, an os::Handle may be a Windows HANDLE, a POSIX
// fie descriptor, a Fuchsia handle, or a Mach send or receive right.
class Handle {
 public:
  enum class Type {
    kInvalid,
#if defined(OS_WIN) || defined(OS_FUCHSIA)
    kHandle,
#elif defined(OS_MAC)
    kMachSendRight,
    kMachReceiveRight,
#endif
#if defined(OS_POSIX) || defined(OS_FUCHSIA)
    kFileDescriptor,
#endif
  };

  Handle();

#if defined(OS_WIN)
  explicit Handle(HANDLE handle);
#elif defined(OS_FUCHSIA)
  explicit Handle(zx::handle handle);
#elif defined(OS_MAC)
  Handle(mach_port_t port, Type type);
#endif

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  explicit Handle(int fd);
#endif

  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  Handle(Handle&& other);
  Handle& operator=(Handle&& other);

  ~Handle();

  static bool ToIpczOSHandle(Handle handle, IpczOSHandle* os_handle);
  static Handle FromIpczOSHandle(const IpczOSHandle& os_handle);

  Type type() const { return type_; }

  void reset();

  // Relinquishes ownership of the underlying handle, regardless of type, and
  // discards its value. To release and obtain the underlying handle value, use
  // one of the specific |Release*()| methods below.
  void release();

  // Duplicates the underlying OS handle, returning a new Handle which owns it.
  Handle Clone() const;

#if defined(OS_WIN)
  bool is_valid() const { return is_valid_handle(); }
  bool is_valid_handle() const { return handle_ != INVALID_HANDLE_VALUE; }
  bool is_handle() const { return type_ == Type::kHandle; }
  HANDLE handle() const { return handle_; }
  HANDLE ReleaseHandle() {
    type_ = Type::kInvalid;
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::swap(handle, handle_);
    return handle;
  }
#elif defined(OS_FUCHSIA)
  bool is_valid() const { return is_valid_fd() || is_valid_handle(); }
  bool is_valid_handle() const { return handle_.is_valid(); }
  bool is_handle() const { return type_ == Type::kHandle; }
  const zx::handle& handle() const { return handle_; }
  zx::handle TakeHandle() {
    if (type_ == Type::kHandle) {
      type_ = Type::kInvalid;
    }
    return std::move(handle_);
  }
  zx_handle_t ReleaseHandle() {
    if (type_ == Type::kHandle) {
      type_ = Type::kInvalid;
    }
    return handle_.release();
  }
#elif defined(OS_MAC)
  bool is_valid() const { return is_valid_fd() || is_valid_mach_port(); }
  bool is_valid_mach_port() const {
    return is_valid_mach_send() || is_valid_mach_receive();
  }

  bool is_valid_mach_send_right() const {
    return mach_send_right_ != MACH_PORT_NULL;
  }
  bool is_mach_send_right() const { return type_ == Type::kMachSendRight; }
  mach_port_t mach_send_right() const { return mach_send_right_; }
  mach_port_t ReleaseMachSendRight() {
    if (type_ == Type::kMachSendRight) {
      type_ = Type::kInvalid;
    }
    return mach_send_right_;
  }

  bool is_valid_mach_receive_right() const {
    return mach_receive_right_ != MACH_PORT_NULL;
  }
  bool is_mach_receive_right() const {
    return type_ == Type::kMachReceiveRight;
  }
  mach_port_t mach_receive_right() const { return mach_receive_right_; }
  mach_port_t ReleaseMachReceiveRight() {
    if (type_ == Type::kMachReceiveRight) {
      type_ = Type::kInvalid;
    }
    return mach_receive_right_;
  }
#elif defined(OS_POSIX)
  bool is_valid() const { return is_valid_fd(); }
#endif

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  bool is_valid_fd() const { return fd_ != -1; }
  bool is_fd() const { return type_ == Type::kFileDescriptor; }
  int fd() const { return fd_; }
  int ReleaseFD() {
    if (type_ == Type::kFileDescriptor) {
      type_ = Type::kInvalid;
    }
    return fd_;
  }
#endif

 private:
  Type type_ = Type::kInvalid;
#if defined(OS_WIN)
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#elif defined(OS_FUCHSIA)
  zx::handle handle_;
#elif defined(OS_MAC)
  mach_port_t mach_send_right_ = MACH_PORT_NULL;
  mach_port_t mach_receive_right_ = MACH_PORT_NULL;
#endif

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  int fd_ = -1;
#endif
};

}  // namespace os
}  // namespace ipcz

#endif  // IPCZ_SRC_OS_HANDLE_H_
