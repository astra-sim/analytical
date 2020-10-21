/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "Topology.hh"
#include <cassert>
#include <iostream>

using namespace Analytical;

Topology::~Topology() = default;

void Topology::connect(NpuId src_id, NpuId dest_id, int dimension) noexcept {
  assert(
      (dimension < configurations.size()) &&
      "[Topology, method connect] dimension out of bound");
  assert(src_id >= 0 && "[Topology, method connect] srcId is negative");
  assert(src_id >= 0 && "[Topology, method connect] destId is negative");

  auto configuration = configurations[dimension];

  auto link_latency = configuration.getLinkLatency();

  links[src_id][dest_id] = Link(link_latency);

  std::cout << "[Topology, method connect] Connect " << src_id << "->"
            << dest_id << " (dim " << dimension << ")" << std::endl;
}

Topology::Latency Topology::route(
    NpuId src_id,
    NpuId dest_id,
    PayloadSize payload_size) noexcept {
  assert(
      (links.find(src_id) != links.end()) &&
      "[Topology, method route] src doesn't have any outgoing link");
  assert(
      (links[src_id].find(dest_id) != links[src_id].end()) &&
      "[Topology, method route] link src->dest doesn't exist");
  return links[src_id][dest_id].send(payload_size);
}

Topology::Latency Topology::serialize(PayloadSize payload_size, int dimension)
    const noexcept {
  assert(
      (dimension < configurations.size()) &&
      "[Topology, method serialize] dimension out of bound");
  return payload_size / configurations[dimension].getLinkBandwidth();
}

Topology::Latency Topology::routerLatency(int dimension) const noexcept {
  assert(
      (dimension < configurations.size()) &&
      "[Topology, method routerLatency] dimension out of bound");
  return configurations[dimension].getRouterLatency();
}

Topology::Latency Topology::nicLatency(int dimension) const noexcept {
  assert(
      (dimension < configurations.size()) &&
      "[Topology, method nicLatency] dimension out of bound");
  return configurations[dimension].getNicLatency();
}

Topology::Latency Topology::hbmLatency(PayloadSize payload_size, int dimension)
    const noexcept {
  assert(
      (dimension < configurations.size()) &&
      "[Topology, method hbmLatency] dimension out of bound");
  auto configuration = configurations[dimension];

  auto hbm_latency = configuration.getHbmLatency();
  auto hbm_bandwidth = configuration.getHbmBandwidth();
  auto hbm_scalar = configuration.getHbmScalar();

  auto latency = hbm_latency + (payload_size / hbm_bandwidth);
  return hbm_scalar * latency;
}

Topology::Latency Topology::criticalLatency(
    Latency link_latency,
    Latency hbm_latency) noexcept {
  if (link_latency >= hbm_latency) {
    // communication bound
    communication_bounds_count++;
    return link_latency;
  }

  // hbm bound
  hbm_bounds_count++;
  return hbm_latency;
}
