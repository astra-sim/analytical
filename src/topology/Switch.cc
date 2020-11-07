/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "Switch.hh"
#include <assert.h>

using namespace Analytical;

Switch::Switch(const TopologyConfigurations& configurations) noexcept {
  this->configurations = configurations;
  packages_count = configurations[0].getPackagesCount();

  // set a switch id
  switch_id = packages_count;

  // 1. Connect all NPUs to a switch: input port
  // 2. Connect the switch to all NPUs: output port
  for (auto npu_id = 0; npu_id < packages_count; npu_id++) {
    connect(npu_id, switch_id, 0); // input port
    connect(switch_id, npu_id, 0); // output port
  }
}

Topology::Latency Switch::send(
    NpuId src_id,
    NpuId dest_id,
    PayloadSize payload_size) noexcept {
  assert(
      0 <= src_id && src_id < packages_count &&
      "[Switch, method send] src_id out of bounds");
  assert(
      0 <= dest_id && dest_id < packages_count &&
      "[Switch, method send] dest_id out of bounds");

  if (src_id == dest_id) {
    // guard statement
    return 0;
  }

  // Switch routing scheme
  //      1. pass source nic
  //      2. move from src to switch
  //      3. add switch delay
  //      4. move from switch to dest
  //      5. pass destination nic
  auto link_latency = serialize(payload_size, 0);
  link_latency += nicLatency(0);
  link_latency += route(src_id, switch_id, payload_size);
  link_latency += routerLatency(0);
  link_latency += route(switch_id, dest_id, payload_size);
  link_latency += nicLatency(0);

  auto hbm_latency = hbmLatency(payload_size, 0);

  return criticalLatency(link_latency, hbm_latency);
}

Topology::NpuAddress Switch::npuIdToAddress(NpuId id) const noexcept {
  return NpuAddress(1, id);
}

Topology::NpuId Switch::npuAddressToId(
    const NpuAddress& address) const noexcept {
  return address[0];
}
