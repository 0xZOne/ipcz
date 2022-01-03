// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_CORE_NODE_H_
#define IPCZ_SRC_CORE_NODE_H_

#include <functional>
#include <utility>

#include "core/driver_transport.h"
#include "core/node_link_memory.h"
#include "core/node_messages.h"
#include "core/node_name.h"
#include "core/portal.h"
#include "ipcz/ipcz.h"
#include "mem/ref_counted.h"
#include "os/memory.h"
#include "os/process.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_map.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"
#include "third_party/abseil-cpp/absl/types/span.h"

namespace ipcz {
namespace core {

class NodeLink;
class Portal;

// A Node controls creation and interconnection of a collection of routers which
// can establish links to and from other routers in other nodes. Every node is
// assigned a globally unique name by a trusted broker node, and nodes may be
// introduced to each other exclusively through such brokers.
class Node : public mem::RefCounted {
 public:
  enum class Type {
    // A broker node assigns its own name and is able to assign names to other
    // nodes upon connection. Brokers are trusted to introduce nodes to each
    // other upon request, and brokers may connect to other brokers in order to
    // share information and effectively bridge two node networks together.
    kBroker,

    // A "normal" (i.e. non-broker) node is assigned a permanent name by the
    // first broker node it connects to, and it can only make contact with other
    // nodes by requesting an introduction from that broker.
    kNormal,
  };

  Node(Type type, const IpczDriver& driver, IpczDriverHandle driver_node);

  Type type() const { return type_; }
  const IpczDriver& driver() const { return driver_; }
  IpczDriverHandle driver_node() const { return driver_node_; }

  // Deactivates all NodeLinks and their underlying driver transports in
  // preparation for this node's imminent destruction.
  void ShutDown();

  // Connects to another node using `driver_transport` for I/O to and from the
  // other node. If the caller has a handle to the process which hosts the
  // remote node, it may be provided by `remote_process`. `initial_portals` is
  // a collection of new portals who will immediately begin to route parcels
  // over a link to the new node, assuming the link is established successfully.
  IpczResult ConnectNode(IpczDriverHandle driver_transport,
                         os::Process remote_process,
                         IpczConnectNodeFlags flags,
                         absl::Span<IpczHandle> initial_portals);

  void SetPortalsWaitingForLink(const NodeName& node_name,
                                absl::Span<const mem::Ref<Portal>> portals);

  // Opens a new pair of directly linked portals on this node and returns
  // references to both of them.
  Portal::Pair OpenPortals();

  NodeName GetAssignedName();

  // Looks up a NodeLink by name. If there's no known link to the named node,
  // this returns null.
  mem::Ref<NodeLink> GetLink(const NodeName& name);

  // Gets a reference to the node's broker link, if it has one.
  mem::Ref<NodeLink> GetBrokerLink();

  // Sets this nodes broker link. The link must also be registered separately
  // via AddLink().
  void SetBrokerLink(mem::Ref<NodeLink> link);

  // Sets this node's assigned name as given by a broker. Must only be called
  // once and only on non-broker nodes.
  void SetAssignedName(const NodeName& name);

  // Registers a new NodeLink for the given `remote_node_name`.
  bool AddLink(const NodeName& remote_node_name, mem::Ref<NodeLink> link);

  // Asynchronously establishes a NodeLink to the named node and invokes
  // `callback` when complete. If it's determined that establishing a link won't
  // be possible, `callback` may be invoked with null.
  //
  // If this node already has a link to the named node, `callback` may be
  // invoked with that link synchronously before EstablishLink() returns. If
  // an introduction request is already in flight for the named node, `callback`
  // is stored and will be invoked one the introduction is complete. Otherise,
  // this node will send an introduction request to its broker before returning
  // and `callback` will be invoked once that request is completed.
  using EstablishLinkCallback = std::function<void(NodeLink*)>;
  void EstablishLink(const NodeName& name, EstablishLinkCallback callback);

  // Handles an incoming request to introduce a new node to this broker
  // indirectly. The sender on the other end of `from_node_link` is already a
  // client of this broker, and they're requesting this introduction on behalf
  // of another (currently brokerless) node.
  bool OnRequestIndirectBrokerConnection(NodeLink& from_node_link,
                                         uint64_t request_id,
                                         mem::Ref<DriverTransport> transport,
                                         os::Process process,
                                         uint32_t num_initial_portals);

  // Handles an incoming introduction request. This message is only accepted by
  // broker nodes. If the broker knows of the node named in `request`, it will
  // send an IntroduceNode message to both that node, and the node which sent
  // this request (at `front_node_link`), with a new driver transport for each
  // one which connects them each other.
  bool OnRequestIntroduction(NodeLink& from_node_link,
                             const msg::RequestIntroduction& request);

  // Handles an incoming introduction to the named node. If `known` is false,
  // then introduction has failed and the rest of the parameters can be ignored.
  // Otherwise `primary_buffer_memory` establishes an initial memory buffer
  // shared between the nodes, and `serialized_transport_data` and
  // `serialized_transport_handles` comprise the complete serialized
  // representation of a new driver transport which can be used for I/O to and
  // from the named node.
  bool OnIntroduceNode(const NodeName& name,
                       bool known,
                       mem::Ref<NodeLinkMemory> link_memory,
                       absl::Span<const uint8_t> serialized_transport_data,
                       absl::Span<os::Handle> serialized_transport_handles);

  // Handles an incoming request to bypass a proxying router on another node.
  bool OnBypassProxy(NodeLink& from_node_link, const msg::BypassProxy& bypass);

  // Registers a callback to be invoked as soon as this node acquires a link to
  // a broker node. If it alread has one, the callback is invoked immediately,
  // before returning from this call.
  using BrokerCallback = std::function<void(mem::Ref<NodeLink> broker_link)>;
  void AddBrokerCallback(BrokerCallback callback);

 private:
  ~Node() override;

  const Type type_;
  const IpczDriver driver_;
  const IpczDriverHandle driver_node_;

  absl::Mutex mutex_;

  // The name assigned to this node by the first broker it connected to. Once
  // assigned, this name remains constant through the life of the node.
  NodeName assigned_name_ ABSL_GUARDED_BY(mutex_);

  // A link to the first broker this node connected to. If this link is broken,
  // the node will lose all its other links too.
  mem::Ref<NodeLink> broker_link_ ABSL_GUARDED_BY(mutex_);

  // Lookup table of broker-assigned node names and links to those nodes. All of
  // these links and their associated names are received by the `broker_link_`
  // if this is a non-broker node. If this is a broker node, these links are
  // either assigned by this node itself, or received from other brokers in the
  // system.
  absl::flat_hash_map<NodeName, mem::Ref<NodeLink>> node_links_
      ABSL_GUARDED_BY(mutex_);

  // A map of other nodes to which this node is waiting for an introduction from
  // `broker_link_`. Once such an introduction is received, all callbacks for
  // that NodeName are executed.
  absl::flat_hash_map<NodeName, std::vector<EstablishLinkCallback>>
      pending_introductions_ ABSL_GUARDED_BY(mutex_);

  // A list of callbacks to be invoked when this node acquires a broker link.
  std::vector<BrokerCallback> broker_callbacks_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace core
}  // namespace ipcz

#endif  // IPCZ_SRC_CORE_NODE_H_
