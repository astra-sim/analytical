/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __RING_HH__
#define __RING_HH__

#include <vector>
#include "Topology.hh"
#include "TopologyConfiguration.hh"

namespace Analytical {
class Ring : public Topology {
 public:
  /**
   * Construct new Ring topology.
   * @param configurations configurations per each dimension
   */
  explicit Ring(const TopologyConfigurations& configurations) noexcept;

  Latency send(NpuId src_id, NpuId dest_id, PayloadSize payload_size) noexcept
      override;

 private:
  using Direction = int; // +1 or -1
  NpuAddress npuIdToAddress(NpuId id) const noexcept override;
  NpuId npuAddressToId(const NpuAddress& address) const noexcept override;

  int packages_count;
  int half_packages_count; // Half of the number of NPUs connected to this ring:
                       // used when computing direction
  bool bidirectional; // whether ring is bidirectional or unidirectional

  /**
   * Compute which direction a packet should proceed.
   *
   * @param src_id
   * @param dest_id
   * @return +1 if a packet should move to (currentId + 1)
   *         -1 if a packet should move to (currentId - 1)
   */
  Direction computeDirection(NpuId src_id, NpuId dest_id) const noexcept;

  /**
   * Take a step towards the given direction and return the destined node after
   * taking single step.
   *
   * @param current_id
   * @param direction direction to take a step
   * @return npuId after taking a step
   */
  NpuId takeStep(NpuId current_id, Direction direction) const noexcept;
};
} // namespace Analytical

#endif
