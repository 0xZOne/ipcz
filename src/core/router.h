// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_CORE_ROUTER_H_
#define IPCZ_SRC_CORE_ROUTER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/incoming_parcel_queue.h"
#include "core/node_name.h"
#include "core/outgoing_parcel_queue.h"
#include "core/parcel.h"
#include "core/routing_id.h"
#include "core/routing_mode.h"
#include "core/sequence_number.h"
#include "core/side.h"
#include "core/trap.h"
#include "ipcz/ipcz.h"
#include "mem/ref_counted.h"
#include "os/handle.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"
#include "third_party/abseil-cpp/absl/types/span.h"

namespace ipcz {
namespace core {

class NodeLink;
struct PortalDescriptor;
class RouterLink;

class Router : public mem::RefCounted {
 public:
  explicit Router(Side side);

  // Pauses or unpauses outgoing parcel transmission.
  void PauseOutgoingTransmission(bool paused);

  // Returns true iff the other side of this Router's route is known to be
  // closed.
  bool IsPeerClosed();

  // Returns true iff the other side of this Router's route is known to be
  // closed, AND all parcels sent from that side have already been retrieved by
  // the application.
  bool IsRouteDead();

  // Fills in an IpczPortalStatus corresponding to the current state of this
  // Router.
  void QueryStatus(IpczPortalStatus& status);

  // Returns true iff this Router's peer link is a LocalRouterLink and its local
  // peer is `other`.
  bool HasLocalPeer(const mem::Ref<Router>& router);

  // Returns true iff sending a parcel of `data_size` towards the other side of
  // the route may exceed the specified `limits` on the receiving end.
  bool WouldOutgoingParcelExceedLimits(size_t data_size,
                                       const IpczPutLimits& limits);

  // Returns true iff accepting an incoming parcel of `data_size` would cause
  // this router's incoming parcel queue to exceed limits specified by `limits`.
  bool WouldIncomingParcelExceedLimits(size_t data_size,
                                       const IpczPutLimits& limits);

  // Attempts to send an outgoing parcel originating from this Router. Called
  // only as a direct result of a Put() call on the router's owning portal.
  IpczResult SendOutgoingParcel(absl::Span<const uint8_t> data,
                                Parcel::PortalVector& portals,
                                std::vector<os::Handle>& os_handles);

  // Closes this side of the Router's own route. Only called on a Router to
  // which a Portal is currently attached, and only by that Portal.
  void CloseRoute();

  // Uses `link` as this Router's new peer link.
  void SetPeer(mem::Ref<RouterLink> link);

  // Uses `link` as this Router's new predecessor link.
  void SetPredecessor(mem::Ref<RouterLink> link);

  // Provides the Router with a new successor link to which it should forward
  // all incoming parcels. If the Router is in full proxying mode, it may also
  // listen for outgoing parcels from the same link, to be forwarded to its peer
  // or predecessor.
  void BeginProxyingWithSuccessor(const PortalDescriptor& descriptor,
                                  mem::Ref<RouterLink> link);

  // Accepts a parcel routed here from `link` via `routing_id`, which is
  // determined to be either an incoming or outgoing parcel based on the source
  // and current routing mode.
  bool AcceptParcelFrom(NodeLink& link, RoutingId routing_id, Parcel& parcel);

  // Accepts an incoming parcel routed here from some other Router. What happens
  // to the parcel depends on the Router's current RoutingMode and established
  // links to other Routers.
  bool AcceptIncomingParcel(Parcel& parcel);

  // Accepts an outgoing parcel router here from some other Router. What happens
  // to the parcel depends on the Router's current RoutingMode and its
  // established links to other Routers.
  bool AcceptOutgoingParcel(Parcel& parcel);

  // Accepts notification that one `side` of this route has been closed.
  // Depending on current routing mode and established links, this notification
  // may be propagated elsewhere by this Router.
  void AcceptRouteClosure(Side side, SequenceNumber sequence_length);

  // Retrieves the next available incoming parcel from this Router, if present.
  IpczResult GetNextIncomingParcel(void* data,
                                   uint32_t* num_bytes,
                                   IpczHandle* portals,
                                   uint32_t* num_portals,
                                   IpczOSHandle* os_handles,
                                   uint32_t* num_os_handles);
  IpczResult BeginGetNextIncomingParcel(const void** data,
                                        uint32_t* num_data_bytes,
                                        uint32_t* num_portals,
                                        uint32_t* num_os_handles);
  IpczResult CommitGetNextIncomingParcel(uint32_t num_data_bytes_consumed,
                                         IpczHandle* portals,
                                         uint32_t* num_portals,
                                         IpczOSHandle* os_handles,
                                         uint32_t* num_os_handles);

  IpczResult AddTrap(std::unique_ptr<Trap> trap);
  IpczResult ArmTrap(Trap& trap,
                     IpczTrapConditionFlags& satistfied_conditions,
                     IpczPortalStatus* status);
  IpczResult RemoveTrap(Trap& trap);

  mem::Ref<Router> Serialize(PortalDescriptor& descriptor);
  static mem::Ref<Router> Deserialize(const PortalDescriptor& descriptor);

  bool InitiateProxyBypass(NodeLink& requesting_node_link,
                           RoutingId requesting_routing_id,
                           const NodeName& proxy_peer_node_name,
                           RoutingId proxy_peer_routing_id,
                           absl::uint128 bypass_key,
                           bool notify_predecessor);

 private:
  friend class LocalRouterLink;

  ~Router() override;

  void FlushParcels();

  const Side side_;

  absl::Mutex mutex_;
  SequenceNumber outgoing_sequence_length_ ABSL_GUARDED_BY(mutex_) = 0;
  RoutingMode routing_mode_ ABSL_GUARDED_BY(mutex_) = RoutingMode::kActive;
  mem::Ref<RouterLink> peer_ ABSL_GUARDED_BY(mutex_);
  mem::Ref<RouterLink> successor_ ABSL_GUARDED_BY(mutex_);
  mem::Ref<RouterLink> predecessor_ ABSL_GUARDED_BY(mutex_);
  int num_outgoing_transmission_blockers_ ABSL_GUARDED_BY(mutex_) = 0;
  OutgoingParcelQueue outgoing_parcels_ ABSL_GUARDED_BY(mutex_);
  IncomingParcelQueue incoming_parcels_ ABSL_GUARDED_BY(mutex_);
  bool peer_closure_propagated_ ABSL_GUARDED_BY(mutex_) = false;
  IpczPortalStatus status_ ABSL_GUARDED_BY(mutex_) = {sizeof(status_)};
  TrapSet traps_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace core
}  // namespace ipcz

#endif  // IPCZ_SRC_CORE_ROUTER_H_