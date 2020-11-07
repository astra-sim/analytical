/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "Ring_AllToAll_Switch.hh"
#include <assert.h>

using namespace Analytical;

Ring_AllToAll_Switch::Ring_AllToAll_Switch(
    const TopologyConfigurations& configurations) noexcept {
  this->configurations = configurations;

  // ring configs
  ring_size = configurations[0].getPackagesCount();
  half_ring_size = ring_size / 2;
  bidirectional = configurations[0].getTopologyShapeConfigs()[0] >= 0;

  // all-to-all configs
  all_to_all_size = configurations[1].getPackagesCount();
  node_packages_count = ring_size * all_to_all_size;

  // switch configs
  switch_size = configurations[2].getPackagesCount();

  // topology configs
  packages_count = ring_size * all_to_all_size * switch_size;

  // connect rings
  for (int start_offset = 0; start_offset < packages_count;
       start_offset += ring_size) {
    for (int offset = 0; offset < (ring_size - 1); offset++) {
      auto src = start_offset + offset;
      auto dest = src + 1;
      connect(src, dest, 0);
    }

    auto src = start_offset + (ring_size - 1);
    auto dest = start_offset;
    connect(src, dest, 0);
  }

  if (bidirectional) {
    for (int start_offset = 0; start_offset < packages_count;
         start_offset += ring_size) {
      for (int offset = ring_size - 1; offset > 0; offset--) {
        auto src = start_offset + offset;
        auto dest = src - 1;
        connect(src, dest, 0);
      }

      auto src = start_offset;
      auto dest = start_offset + (ring_size - 1);
      connect(src, dest, 0);
    }
  }

  // connect all-to-all
  for (int switch_offset = 0; switch_offset < switch_size; switch_offset++) {
    for (int ring_offset = 0; ring_offset < ring_size; ring_offset++) {
      for (int i = 0; i < all_to_all_size; i++) {
        for (int j = 0; j < all_to_all_size; j++) {
          // When i == j, links gets constructed here
          // but the link never gets utilized
          auto src = (i * ring_size) + ring_offset +
              (switch_offset * node_packages_count);
          auto dest = (j * ring_size) + ring_offset +
              (switch_offset * node_packages_count);
          connect(src, dest, 1);
        }
      }
    }
  }

  // connect switch
  switch_id = packages_count;
  for (int i = 0; i < packages_count; i++) {
    connect(i, switch_id, 2); // input port
    connect(switch_id, i, 2); // output port
  }
}

Topology::Latency Ring_AllToAll_Switch::send(
    NpuId src_id,
    NpuId dest_id,
    PayloadSize payload_size) noexcept {
  assert(
      0 <= src_id && src_id < packages_count &&
      "[Ring_AllToAll, method send] src_id out of bounds");
  assert(
      0 <= dest_id && dest_id < packages_count &&
      "[Ring_AllToAll, method send] dest_id out of bounds");

  if (src_id == dest_id) {
    // guard statement
    return 0;
  }

  auto current_address = npuIdToAddress(src_id);
  auto dest_address = npuIdToAddress(dest_id);

  auto link_latency = 0.0;

  if (current_address[2] != dest_address[2]) {
    // node differs; use scale-out switch
    link_latency += serialize(payload_size, 2);
    link_latency += nicLatency(2);
    link_latency += route(src_id, switch_id, payload_size);
    link_latency += routerLatency(2);
    link_latency += route(switch_id, dest_id, payload_size);
    link_latency += nicLatency(2);
  } else {
    // within the same node; use scale-up networks

    if (current_address[0] != dest_address[0]) {
      // use ring network on the dimension 0

      // compute which direction to move
      auto direction = computeDirection(current_address[0], dest_address[0]);

      // serialize packet
      link_latency += serialize(payload_size, 0);
      link_latency += nicLatency(0);

      // move towards direction until reaching destination
      while (current_address[0] != dest_address[0]) {
        // compute current id
        auto current_id = npuAddressToId(current_address);

        // compute next id
        current_address[0] = takeStep(current_address[0], direction);
        auto next_id = npuAddressToId(current_address);

        // route
        link_latency += route(current_id, next_id, payload_size);
      }

      link_latency += nicLatency(0);
    }

    if (current_address[1] != dest_address[1]) {
      // use all-to-all network on the dimension 1
      // this forwards the packet to the destination

      auto current_id = npuAddressToId(current_address);

      link_latency += serialize(payload_size, 1);
      link_latency += nicLatency(1);
      link_latency += route(current_id, dest_id, payload_size);
      link_latency += nicLatency(1);
    }
  }

  auto hbm_latency = hbmLatency(payload_size, 0);

  return criticalLatency(link_latency, hbm_latency);
}

Topology::NpuAddress Ring_AllToAll_Switch::npuIdToAddress(
    NpuId id) const noexcept {
  // trivial dimensions
  auto node_id = id / node_packages_count;
  auto ring_id = id % ring_size;

  // all-to-all id
  auto node_offset = id % node_packages_count;
  auto all_to_all_id = node_offset / ring_size;

  auto address = NpuAddress();
  address.emplace_back(ring_id);
  address.emplace_back(all_to_all_id);
  address.emplace_back(node_id);

  return address;
}

Topology::NpuId Ring_AllToAll_Switch::npuAddressToId(
    const NpuAddress& address) const noexcept {
  auto id = address[0] // ring ID
      + (address[1] * ring_size) // all-to-all offset
      + (address[2] * node_packages_count); // switch offset
  return id;
}

Ring_AllToAll_Switch::Direction Ring_AllToAll_Switch::computeDirection(
    NpuId src_id,
    NpuId dest_id) const noexcept {
  if (!bidirectional) {
    // unidirectional
    return 1;
  }

  // bidirectional: compute shortest path
  if (src_id < dest_id) {
    auto distance = dest_id - src_id;
    return (distance <= half_ring_size) ? 1 : -1;
  }

  auto distance = src_id - dest_id;
  return (distance <= half_ring_size) ? -1 : 1;
}

Ring_AllToAll_Switch::NpuId Ring_AllToAll_Switch::takeStep(
    NpuId current_id,
    Direction direction) const noexcept {
  // compute next id
  auto next_id = current_id + direction;

  if (next_id >= ring_size) {
    // out of positive bounds
    next_id %= ring_size;
  } else if (next_id < 0) {
    // out of negative bounds
    next_id = ring_size + (next_id % ring_size);
  }

  return next_id;
}
