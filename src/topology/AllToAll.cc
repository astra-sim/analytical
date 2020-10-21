/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "AllToAll.hh"

using namespace Analytical;

AllToAll::AllToAll(
    const TopologyConfigurations& configurations) noexcept {
  this->configurations = configurations;
  packages_count = configurations[0].getPackagesCount();

  // connect all packages directly
  for (int n1 = 0; n1 < (packages_count - 1); n1++) {
    for (int n2 = (n1 + 1); n2 < packages_count; n2++) {
      connect(n1, n2, 0);
      connect(n2, n1, 0);
    }
  }
}

Topology::Latency AllToAll::send(
    NpuId src_id,
    NpuId dest_id,
    PayloadSize payload_size) noexcept {
  assert(0 <= src_id && src_id < packages_count && "[AllToAll, method send] src_id out of bounds");
  assert(0 <= dest_id && dest_id < packages_count && "[AllToAll, method send] dest_id out of bounds");

  if (src_id == dest_id) {
    // guard statement
    return 0;
  }

  // 1. Source nic latency
  // 2. route packet from src to dest
  // 3. Dest nic latency
  auto link_latency = serialize(payload_size, 0);
  link_latency += nicLatency(0);
  link_latency += route(src_id, dest_id, payload_size);
  link_latency += nicLatency(0);

  auto hbm_latency = hbmLatency(payload_size, 0);

  return criticalLatency(link_latency, hbm_latency);
}

Topology::NpuAddress AllToAll::npuIdToAddress(NpuId id) const noexcept {
  return NpuAddress(1, id);
}

Topology::NpuId AllToAll::npuAddressToId(
    const NpuAddress& address) const noexcept {
  return address[0];
}
