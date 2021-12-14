// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// no-include-guard-because-multiply-included

// This file defines the internal messages which can be sent on a NodeLink
// between two ipcz nodes.

// This needs to be incremented any time changes are made to these definitions.
IPCZ_PROTOCOL_VERSION(0)

// Initial greeting sent by every node via the ConnectNode() API.
IPCZ_MSG_NO_REPLY(Connect, IPCZ_MSG_ID(0), IPCZ_MSG_VERSION(0))
  // The name of the sending node. Names should be randomly generated once at
  // the start of a node's lifetime. They are large and random for global
  // uniqueness, not for security reasons.
  IPCZ_MSG_PARAM(NodeName, name)

  // The highest protocol version known and desired by the sender.
  IPCZ_MSG_PARAM(uint32_t, protocol_version)

  // The number of initial portals assumed on the sender's end of the
  // connection. If there is a mismatch between the number sent by each node on
  // an initial connection, the node which sent the larger number should behave
  // as if its excess portals have observed peer closure.
  IPCZ_MSG_PARAM(uint32_t, num_initial_portals)

  // An optional handle to a shared memory object which can be used to allocate
  // chunks of shared state.
  //
  // TODO: decide on resolution here if both/neither ends provide a handle. for
  // now, ConnectNode() only goes between broker and non-broker, and we adopt
  // the convention that the broker always sends a handle and the non-broker
  // never does. will need something better to support non-broker to non-broker
  // ConnectNode().
  //
  // one idea would be to leave the transport in a state not suitable for portal
  // operation until someone provides a memory object (if neither does); and to
  // introduce a separate message for adding a new link buffer, which we'll need
  // anyway. also if both send a memory handle, we could e.g. assign one as
  // the primary and the other as an auxilliary buffer, based on the relative
  // ordering of the `name` given on each side; but that requires both sides
  // waiting for a Connect from the other side before parcels can flow in either
  // direction, whereas otherwise at least one side could begin operating
  // immediately.
  IPCZ_MSG_HANDLE_OPTIONAL(link_state_memory)
IPCZ_MSG_END()

// Notifies a node that the route has been closed on one side. If this arrives
// at a router from an inward facing or bridge link, it implicitly pertains to
// the router's own side of the route. Otherwise it indicates that the other
// side of the route has been closed. In either case, `sequence_length` is the
// total number of parcels transmitted from the closed side before closing.
IPCZ_MSG_NO_REPLY(RouteClosed, IPCZ_MSG_ID(2), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
  IPCZ_MSG_PARAM(SequenceNumber, sequence_length)
IPCZ_MSG_END()

// Sent by a non-broker node to a broker node. Requests that the broker provide
// a new NodeLink to both the sender and the node identified by `name`, linking
// the two nodes together and allowing them to communicate directly. This
// message has no reply.
//
// If the broker does not know the node named `name`, it will send an
// IntroduceNode message back to the sender with empty handles, indicating
// failure. Otherwise it will send an IntroduceNode message to both the sender
// and the node identified by `name`, with complementary transport descriptors
// attached to each, and an os::Memory handle in `link_state_memory` which each
// side can use to map a shared (zero-initialized) NodeLinkState.
IPCZ_MSG_NO_REPLY(RequestIntroduction, IPCZ_MSG_ID(3), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(NodeName, name)
IPCZ_MSG_END()

// Informs the recipient that its predecessor has become a half-proxy. In the
// simplest half-proxying scenario (moving an active portal with an active peer)
// this message is unused and the relevant information is instead conveyed
// directly by the serialized portal within the parcel that moves it.
//
// However, in cases where a moved portal cannot enter a half-proxying state
// (because either it had no peer, or its peer was buffering or half-proxying
// at the time) the portal becomes a full proxy. Only once a full proxy obtains
// a peer link to an active peer can it decay to a half-proxy.
//
// This message is used to implement that decay operation. Once a full proxy
// obtains a peer link to an active peer, it generates a random 128-bit key
// as described on BypassProxy above and shares it in the peer link's shared
// state. It also then sends this InitiateProxyBypass message to its successor,
// with the same key and some information about its own peer link. The successor
// then uses this information to construct and send a BypassProxy message to the
// named peer.
IPCZ_MSG_NO_REPLY(InitiateProxyBypass, IPCZ_MSG_ID(5), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
  IPCZ_MSG_PARAM(uint64_t, reserved0)
  IPCZ_MSG_PARAM(NodeName, proxy_peer_name)
  IPCZ_MSG_PARAM(RoutingId, proxy_peer_routing_id)
  IPCZ_MSG_PARAM(absl::uint128, bypass_key)
IPCZ_MSG_END()

// Simultaneously informs the recipient that its peer link is connected to a
// half-proxying portal, and requests that this link be replaced immediately
// with a more direct link to the proxy's own destination; which is the sender
// of this message:
//
//  recipient R === [peer link] ==>  proxy P  === [successor link] ==> sender S
//     \                                                               /
//      \__<<<<<<<<<<<<<<<<<[ BypassProxy message ]<<<<<<<<<<<<<<<<___/
//
// In this diagram, portal P was at some point a mutual peer of portal R, and
// portal S did not exist. The active portal at P was then relocated to S on a
// different node, and left in P's place was a half-proxy: a portal that exists
// only to forward parcels to its successor (S).
//
// As part of this operation, P generated a random key and stashed it in the
// shared state of the peer link between R and P. The same key was subsequently
// shared with S (and only S), along with the identity of R and the routing ID
// of the peer link between R and P.
//
// S sends this BypassProxy message to R using the same shared secret key,
// allowing R to trust the request and immediately stop using the peer link to
// P, replacing it instead with a new peer link directly to S.
//
// R also sends a StopProxying message to P just before this operation completes
// to inform P of the last in-flight parcel already sent to P and therefore the
// last parcel P needs to accept and forward to S before it can cease to exist.
IPCZ_MSG_NO_REPLY(BypassProxy, IPCZ_MSG_ID(6), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(NodeName, proxy_name)
  IPCZ_MSG_PARAM(RoutingId, proxy_routing_id)
  IPCZ_MSG_PARAM(RoutingId, new_routing_id)
  IPCZ_MSG_PARAM(absl::uint128, bypass_key)
  IPCZ_MSG_PARAM(SequenceNumber, proxied_outbound_sequence_length)
IPCZ_MSG_END()

// Equivalent to BypassProxy, but used only when the requesting proxy and its
// bypass target live on the same node. In this case there's no need for the
// bypasser to authenticate any request to the target, because the target is
// made aware of the bypass as soon as its local peer initiates it. The bypasser
// receiving this message need only decay its link over `routing_id` and replace
// it with a link over `new_routing_id`. `sequence_length` indicates the length
// of the sequence already sent or in-flight over `routing_id` from the proxy,
// and the bypasser must send a StopProxyingToLocalPeer back to the proxy with
// this length and the length of its own outbound sequence just before switching
// to the new link.
IPCZ_MSG_NO_REPLY(BypassProxyToSameNode, IPCZ_MSG_ID(7), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
  IPCZ_MSG_PARAM(RoutingId, new_routing_id)
  IPCZ_MSG_PARAM(SequenceNumber, sequence_length)
IPCZ_MSG_END()

// Informs the recipient that the portal on `routing_id` for this NodeLink can
// cease to exist once it has received and forwarded parcels up to the
// specified sequence length in each direction.
IPCZ_MSG_NO_REPLY(StopProxying, IPCZ_MSG_ID(8), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
  IPCZ_MSG_PARAM(SequenceNumber, inbound_sequence_length)
  IPCZ_MSG_PARAM(SequenceNumber, outbound_sequence_length)
IPCZ_MSG_END()

// Informs the recipient that it has been bypassed by the sender in favor of a
// direct route to the recipient's local peer. This is essentially a reply to
// BypassProxyToSameNode.
IPCZ_MSG_NO_REPLY(StopProxyingToLocalPeer, IPCZ_MSG_ID(9), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
  IPCZ_MSG_PARAM(SequenceNumber, sequence_length)
IPCZ_MSG_END()

// Informs the recipient that its decaying outward link (implicitly going to a
// decaying proxy) will only receive inbound parcels up to but not including the
// given `sequence_length`.
IPCZ_MSG_NO_REPLY(ProxyWillStop, IPCZ_MSG_ID(10), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
  IPCZ_MSG_PARAM(SequenceNumber, sequence_length)
IPCZ_MSG_END()

IPCZ_MSG_NO_REPLY(DecayUnblocked, IPCZ_MSG_ID(11), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
IPCZ_MSG_END()

// Requests that the receiving Router log a description of itself and forward
// this request along the same direction in which it was received.
IPCZ_MSG_NO_REPLY(LogRouteTrace, IPCZ_MSG_ID(12), IPCZ_MSG_VERSION(0))
  IPCZ_MSG_PARAM(RoutingId, routing_id)
IPCZ_MSG_END()
