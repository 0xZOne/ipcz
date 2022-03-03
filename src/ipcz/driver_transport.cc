#include "ipcz/driver_transport.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "build/build_config.h"
#include "ipcz/ipcz.h"
#include "ipcz/message_internal.h"
#include "ipcz/node.h"
#include "third_party/abseil-cpp/absl/base/macros.h"
#include "third_party/abseil-cpp/absl/types/span.h"
#include "util/handle_util.h"
#include "util/ref_counted.h"

#if BUILDFLAG(IS_WIN)
#define IPCZ_CDECL __cdecl
#else
#define IPCZ_CDECL
#endif

namespace ipcz {

namespace {

IpczResult IPCZ_CDECL NotifyTransport(IpczHandle transport,
                                      const uint8_t* data,
                                      uint32_t num_bytes,
                                      const IpczDriverHandle* driver_handles,
                                      uint32_t num_driver_handles,
                                      IpczTransportActivityFlags flags,
                                      const void* options) {
  if (transport == IPCZ_INVALID_HANDLE) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  if (flags & IPCZ_TRANSPORT_ACTIVITY_DEACTIVATED) {
    Ref<DriverTransport> doomed_transport(RefCounted::kAdoptExistingRef,
                                          ToPtr<DriverTransport>(transport));
    return IPCZ_RESULT_OK;
  }

  DriverTransport& t = ToRef<DriverTransport>(transport);
  if (flags & IPCZ_TRANSPORT_ACTIVITY_ERROR) {
    t.NotifyError();
    return IPCZ_RESULT_OK;
  }

  return t.Notify(DriverTransport::Message(
      absl::MakeSpan(data, num_bytes),
      absl::MakeSpan(driver_handles, num_driver_handles)));
}

}  // namespace

DriverTransport::Descriptor::Descriptor() = default;

DriverTransport::Descriptor::Descriptor(Descriptor&&) = default;

DriverTransport::Descriptor& DriverTransport::Descriptor::operator=(
    Descriptor&&) = default;

DriverTransport::Descriptor::~Descriptor() = default;

DriverTransport::Data::Data() = default;

DriverTransport::Data::Data(absl::Span<const uint8_t> data) : Span(data) {}

DriverTransport::Data::Data(absl::Span<const char> str)
    : Span(reinterpret_cast<const uint8_t*>(str.data()), str.size()) {}

absl::Span<const char> DriverTransport::Data::AsString() const {
  return absl::MakeSpan(reinterpret_cast<const char*>(data()), size());
}

DriverTransport::Message::Message(Data data) : data(data) {}

DriverTransport::Message::Message(Data data,
                                  absl::Span<const IpczDriverHandle> handles)
    : data(data), handles(handles) {}

DriverTransport::Message::Message(const Message&) = default;

DriverTransport::Message& DriverTransport::Message::operator=(const Message&) =
    default;

DriverTransport::Message::~Message() = default;

DriverTransport::DriverTransport(DriverObject transport)
    : transport_(std::move(transport)) {}

DriverTransport::~DriverTransport() = default;

// static
DriverTransport::Pair DriverTransport::CreatePair(Ref<Node> node) {
  IpczDriverHandle transport0;
  IpczDriverHandle transport1;
  IpczResult result = node->driver().CreateTransports(
      node->driver_node(), IPCZ_NO_FLAGS, nullptr, &transport0, &transport1);
  ABSL_ASSERT(result == IPCZ_RESULT_OK);
  auto first = MakeRefCounted<DriverTransport>(DriverObject(node, transport0));
  auto second = MakeRefCounted<DriverTransport>(
      DriverObject(std::move(node), transport1));
  return {std::move(first), std::move(second)};
}

IpczDriverHandle DriverTransport::Release() {
  return transport_.release();
}

IpczResult DriverTransport::Activate() {
  // Acquire a self-reference, balanced in NotifyTransport() when the driver
  // invokes its activity handler with IPCZ_TRANSPORT_ACTIVITY_DEACTIVATED.
  IpczHandle handle = ToHandle(WrapRefCounted(this).release());
  return transport_.node()->driver().ActivateTransport(
      transport_.handle(), handle, NotifyTransport, IPCZ_NO_FLAGS, nullptr);
}

IpczResult DriverTransport::Deactivate() {
  return transport_.node()->driver().DeactivateTransport(
      transport_.handle(), IPCZ_NO_FLAGS, nullptr);
}

IpczResult DriverTransport::TransmitMessage(const Message& message) {
  return transport_.node()->driver().Transmit(
      transport_.handle(), message.data.data(),
      static_cast<uint32_t>(message.data.size()), message.handles.data(),
      static_cast<uint32_t>(message.handles.size()), IPCZ_NO_FLAGS, nullptr);
}

IpczResult DriverTransport::Notify(const Message& message) {
  // Do some basic validation of the header vs the message contents.
  if (message.data.size() < sizeof(internal::MessageHeader)) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  const auto& header =
      *reinterpret_cast<const internal::MessageHeader*>(message.data.data());
  if (header.size < sizeof(internal::MessageHeader)) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  ABSL_ASSERT(listener_);
  return listener_->OnTransportMessage(message);
}

void DriverTransport::NotifyError() {
  ABSL_ASSERT(listener_);
  listener_->OnTransportError();
}

}  // namespace ipcz
