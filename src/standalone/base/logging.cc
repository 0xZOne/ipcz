// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "standalone/base/logging.h"

#include <atomic>
#include <iostream>

#include "build/build_config.h"
#include "third_party/abseil-cpp/absl/base/log_severity.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/types.h>
#include <unistd.h>
#endif

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace ipcz {
namespace standalone {

namespace {

std::atomic_int g_verbosity_level{0};

}  // namespace

LogMessage::LogMessage(const char* file, int line, Level level) {
  stream_ << "[";
#if BUILDFLAG(IS_POSIX)
  stream_ << getpid() << ":" << gettid() << ":";
  const char* trimmed_file = strrchr(file, '/') + 1;
#elif BUILDFLAG(IS_WIN)
  const char* trimmed_file = file;
  stream_ << (::GetCurrentProcessId()) << ":" << ::GetCurrentThreadId() << ":";
#else
  const char* trimmed_file = file;
#endif
  stream_ << absl::LogSeverityName(level) << ":"
          << (trimmed_file ? trimmed_file : file) << "(" << line << ")] ";
}

LogMessage::~LogMessage() {
  std::cerr << stream_.str() << std::endl;
}

void SetVerbosityLevel(int level) {
  g_verbosity_level.store(level, std::memory_order_relaxed);
}

int GetVerbosityLevel() {
  return g_verbosity_level.load(std::memory_order_relaxed);
}

}  // namespace standalone
}  // namespace ipcz
