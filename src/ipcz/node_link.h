// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_IPCZ_NODE_LINK_H_
#define IPCZ_SRC_IPCZ_NODE_LINK_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <type_traits>

#include "ipcz/buffer_id.h"
#include "ipcz/driver_memory.h"
#include "ipcz/driver_transport.h"
#include "ipcz/fragment_ref.h"
#include "ipcz/link_side.h"
#include "ipcz/message_internal.h"
#include "ipcz/node.h"
#include "ipcz/node_link_memory.h"
#include "ipcz/node_messages.h"
#include "ipcz/node_name.h"
#include "ipcz/remote_router_link.h"
#include "ipcz/router_link_state.h"
#include "ipcz/sequence_number.h"
#include "ipcz/sublink_id.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_map.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/span.h"
#include "util/function.h"
#include "util/os_handle.h"
#include "util/os_process.h"
#include "util/ref_counted.h"

namespace ipcz {

class Node;
class RemoteRouterLink;
class Router;
class RouterLink;

// A NodeLink instance encapsulates all communication from its owning node to
// exactly one other remote node in the sytem. Each NodeLink manages a
// DriverTransport for general-purpose I/O to and from the remote node, as well
// as a NodeLinkMemory instance for dynamic allocation from a pool of memory
// shared between the two nodes.
//
// NodeLinks may also allocate an arbitrary number of sublinks which are used
// to multiplex the link and facilitate point-to-point communication between
// specific Router instances on either end.
class NodeLink : public RefCounted, private DriverTransport::Listener {
 public:
  struct Sublink {
    Sublink(Ref<RemoteRouterLink> link, Ref<Router> receiver);
    Sublink(Sublink&&);
    Sublink(const Sublink&);
    Sublink& operator=(Sublink&&);
    Sublink& operator=(const Sublink&);
    ~Sublink();

    Ref<RemoteRouterLink> router_link;
    Ref<Router> receiver;
  };

  static Ref<NodeLink> Create(Ref<Node> node,
                              LinkSide link_side,
                              const NodeName& local_node_name,
                              const NodeName& remote_node_name,
                              Node::Type remote_node_type,
                              uint32_t remote_protocol_version,
                              Ref<DriverTransport> transport,
                              OSProcess remote_process,
                              Ref<NodeLinkMemory> memory);

  const Ref<Node>& node() const { return node_; }
  LinkSide link_side() const { return link_side_; }
  const NodeName& local_node_name() const { return local_node_name_; }
  const NodeName& remote_node_name() const { return remote_node_name_; }
  Node::Type remote_node_type() const { return remote_node_type_; }
  uint32_t remote_protocol_version() const { return remote_protocol_version_; }
  const Ref<DriverTransport>& transport() const { return transport_; }

  NodeLinkMemory& memory() { return *memory_; }
  const NodeLinkMemory& memory() const { return *memory_; }

  // Binds `sublink` on this NodeLink to the given `router`. `link_side`
  // specifies which side of the link this end identifies as (A or B), and
  // `type` specifies the type of link this is, from the perspective of
  // `router`.
  //
  // If `link_state_fragment` is non-null, the given fragment contains the
  // shared RouterLinkState structure for the new route. Only central links
  // require a RouterLinkState.
  Ref<RemoteRouterLink> AddRemoteRouterLink(
      SublinkId sublink,
      FragmentRef<RouterLinkState> link_state,
      LinkType type,
      LinkSide side,
      Ref<Router> router);

  // Removes the route specified by `sublink`. Once removed, any messages
  // received for that sublink are ignored.
  bool RemoveRemoteRouterLink(SublinkId sublink);

  // Retrieves the Router and RemoteRouterLink currently bound to `sublink`
  // on this NodeLink.
  absl::optional<Sublink> GetSublink(SublinkId sublink);

  // Retrieves only the Router currently bound to `sublink` on this NodeLink.
  Ref<Router> GetRouter(SublinkId sublink);

  // Permanently deactivates this NodeLink. Once this call returns the NodeLink
  // will no longer receive transport messages. It may still be used to transmit
  // outgoing messages, but it cannot be reactivated.
  void Deactivate();

  template <typename T>
  void Transmit(T& message) {
    static_assert(std::is_base_of<internal::MessageBase, T>::value,
                  "Invalid message type");
    TransmitMessage(message, T::kMetadata);
  }

  // Asks the broker on the other end of this link to accept a new node for
  // introduction into the network. Normally nodes connect directly to a broker,
  // but in some cases a non-broker may connect instead to another non-broker to
  // request inheritance of its broker in order to join the same network. This
  // message is sent to the broker by an established non-broker on behalf of the
  // new non-broker attempting to join the network.
  using IndirectBrokerConnectionCallback =
      Function<void(const NodeName&, uint32_t num_remote_portals)>;
  void RequestIndirectBrokerConnection(
      Ref<DriverTransport> transport,
      OSProcess new_node_process,
      size_t num_initial_portals,
      IndirectBrokerConnectionCallback callback);

  // Asks the broker on the other end of this link to introduce the local node
  // on this side of the link to the named node. This will always elicit an
  // eventual IntroduceNode message from the broker, even if only to indicate
  // failure.
  void RequestIntroduction(const NodeName& name);

  // Introduces the remote node to the node named `name`, giving it a new
  // DriverTransport it can use to communicate with that node.
  void IntroduceNode(const NodeName& name,
                     LinkSide link_side,
                     Ref<DriverTransport> transport,
                     DriverMemory primary_buffer);

  // Sends a request to the remote node to have one of its routers reconfigured
  // with a new peer link. Specifically, whatever router has a peer
  // RemoteRouterLink to `proxy_peer_node_name` on `proxy_peer_sublink` is
  // reconfigured to instead adopt a new RemoteRouterLink as its peer, on this
  // NodeLink with a newly allocated sublink.  `new_peer` becomes the new
  // peer of the reconfigured remote router, assuming it's able to authenticate
  // the source of this request.
  bool BypassProxy(const NodeName& proxy_name,
                   SublinkId proxy_sublink,
                   SequenceNumber proxy_outbound_sequence_length,
                   Ref<Router> new_peer);

  // Sends a new shared memory object to the remote endpoint to be associated
  // with BufferId within this link's associated NodeLinkMemory, and to be used
  // to dynamically allocate fragments of `fragment_size` bytes. The BufferId
  // must have already been reserved locally by this NodeLink.
  void AddFragmentAllocatorBuffer(BufferId buffer_id,
                                  uint32_t fragment_size,
                                  DriverMemory memory);

  // Sends a request to allocate a new shared memory region and invokes
  // `callback` once the request is fulfilled.
  using RequestMemoryCallback = Function<void(DriverMemory)>;
  void RequestMemory(uint32_t size, RequestMemoryCallback callback);

 private:
  NodeLink(Ref<Node> node,
           LinkSide link_side,
           const NodeName& local_node_name,
           const NodeName& remote_node_name,
           Node::Type remote_node_type,
           uint32_t remote_protocol_version,
           Ref<DriverTransport> transport,
           OSProcess remote_process,
           Ref<NodeLinkMemory> memory);
  ~NodeLink() override;

  void TransmitMessage(internal::MessageBase& message,
                       absl::Span<const internal::ParamMetadata> metadata);

  // DriverTransport::Listener:
  IpczResult OnTransportMessage(
      const DriverTransport::Message& message) override;
  void OnTransportError() override;

  // All of these methods correspond directly to remote calls from another node,
  // either through NodeLink (for OnIntroduceNode) or via RemoteRouterLink (for
  // everything else).
  bool OnConnectFromBrokerToNonBroker(
      msg::ConnectFromBrokerToNonBroker& connect);
  bool OnConnectFromNonBrokerToBroker(
      msg::ConnectFromNonBrokerToBroker& connect);
  bool OnConnectToBrokerIndirect(msg::ConnectToBrokerIndirect& connect);
  bool OnConnectFromBrokerIndirect(msg::ConnectFromBrokerIndirect& connect);
  bool OnConnectFromBrokerToBroker(msg::ConnectFromBrokerToBroker& connect);
  bool OnRequestIndirectBrokerConnection(
      msg::RequestIndirectBrokerConnection& request);
  bool OnAcceptIndirectBrokerConnection(
      const msg::AcceptIndirectBrokerConnection& accept);
  bool OnAcceptParcel(msg::AcceptParcel& message);
  bool OnRouteClosed(const msg::RouteClosed& route_closed);
  bool OnSetRouterLinkStateFragment(const msg::SetRouterLinkStateFragment& set);
  bool OnRequestIntroduction(const msg::RequestIntroduction& request);
  bool OnIntroduceNode(msg::IntroduceNode& intro);
  bool OnAddFragmentAllocatorBuffer(msg::AddFragmentAllocatorBuffer& add);
  bool OnStopProxying(const msg::StopProxying& stop);
  bool OnInitiateProxyBypass(const msg::InitiateProxyBypass& request);
  bool OnBypassProxy(const msg::BypassProxy& bypass);
  bool OnBypassProxyToSameNode(const msg::BypassProxyToSameNode& bypass);
  bool OnStopProxyingToLocalPeer(const msg::StopProxyingToLocalPeer& stop);
  bool OnProxyWillStop(const msg::ProxyWillStop& will_stop);
  bool OnFlushRouter(const msg::FlushRouter& flush);
  bool OnRequestMemory(const msg::RequestMemory& request);
  bool OnProvideMemory(msg::ProvideMemory& provide);
  bool OnLogRouteTrace(const msg::LogRouteTrace& log_request);
  bool OnFlushLink(const msg::FlushLink& flush);

  IpczResult FlushSharedMemoryMessages(uint64_t max_sequence_number);
  IpczResult DispatchMessage(const DriverTransport::Message& message);

  const Ref<Node> node_;
  const LinkSide link_side_;
  const NodeName local_node_name_;
  const NodeName remote_node_name_;
  const Node::Type remote_node_type_;
  const uint32_t remote_protocol_version_;
  const Ref<DriverTransport> transport_;
  const OSProcess remote_process_;
  const Ref<NodeLinkMemory> memory_;

  absl::Mutex mutex_;
  bool active_ = true;

  // Messages transmitted from this NodeLink may traverse either the driver
  // transport OR some shared memory queue. Each message is assigned a sequence
  // number to ensure that the receiving node can process them in the intended
  // order. This atomic generates those sequence numbers.
  //
  // Note that this is only incremented for new messages going over the driver
  // transport. Shared memory messages use the sequence number of the most
  // recently transmitted transport message; this is because we're only
  // concerned with preserving ordering where ordering might matter: between
  // a transport and shared memory message sent from the same thread.
  std::atomic<uint64_t> transport_sequence_number_{0};

  using SublinkMap = absl::flat_hash_map<SublinkId, Sublink>;
  SublinkMap sublinks_ ABSL_GUARDED_BY(mutex_);

  uint64_t next_request_id_ ABSL_GUARDED_BY(mutex_) = 0;
  using IndirectBrokerConnectionMap =
      absl::flat_hash_map<uint64_t, IndirectBrokerConnectionCallback>;
  IndirectBrokerConnectionMap pending_indirect_broker_connections_
      ABSL_GUARDED_BY(mutex_);

  using MemortRequestMap =
      absl::flat_hash_map<uint32_t, std::list<RequestMemoryCallback>>;
  MemortRequestMap pending_memory_requests_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace ipcz

#endif  // IPCZ_SRC_IPCZ_NODE_LINK_H_