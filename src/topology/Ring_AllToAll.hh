/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __RING_ALLTOALL_HH__
#define __RING_ALLTOALL_HH__

#include <vector>
#include "Topology.hh"
#include "TopologyConfiguration.hh"

namespace Analytical {
class Ring_AllToAll : public Topology {
 public:
  using TopologyConfigurations = TopologyConfiguration::TopologyConfigurations;

  /**
   * Constrct a switch.
   * @param configurations configuration for each dimensino
   *              - Simple switch has only 1 dim
   */
  explicit Ring_AllToAll(const TopologyConfigurations& configurations) noexcept;

  Latency send(NpuId src_id, NpuId dest_id, PayloadSize payload_size) noexcept
      override;

 private:
  NpuAddress npuIdToAddress(NpuId id) const noexcept override;
  NpuId npuAddressToId(const NpuAddress& address) const noexcept override;

  using Direction = int; // see Ring::Direction

  // See Ring::computeDirection
  Direction computeDirection(NpuId src_id, NpuId dest_id) const noexcept;

  // See Ring::takeStep
  NpuId takeStep(NpuId current_id, Direction direction) const noexcept;

  int packages_count; // the number of packages connected to this switch

  int ring_size; // size (packages_count) of a ring
  int half_ring_size; // ring_size/2, used for direction determination
  bool bidirectional; // whether the ring is bidirectional
  int all_to_all_size; // size (packages_count) of an all-to-all network
};
} // namespace Analytical

#endif
