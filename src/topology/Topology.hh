/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __TOPOLOGY_HH__
#define __TOPOLOGY_HH__

#include <map>
#include <vector>
#include "Link.hh"
#include "TopologyConfiguration.hh"

namespace Analytical {
class Topology {
 public:
  using Latency = TopologyConfiguration::Latency;
  using Bandwidth = TopologyConfiguration::Bandwidth;
  using PayloadSize = TopologyConfiguration::PayloadSize;
  using TopologyConfigurations = TopologyConfiguration::TopologyConfigurations;

  using NpuId = int; // Each NPU's ID is in 'int'
  using NpuAddress =
      std::vector<int>; // NPU's address, denoted by PackageID of each dimension

  /**
   * Virtual destructor for Topology abstract class
   */
  virtual ~Topology() = 0;

  /**
   * Simulate packet transmission from src to dest.
   *
   * 1. This function implements routing algorithm of a topology
   * 2. Latency is accumulated as required during the transmission.
   * 3. Return the transmission latency after the transmission simuation is
   * done.
   *
   * @param src_id
   * @param dest_id
   * @param payload_size
   * @return latency of the transmission
   */
  virtual Latency send(
      NpuId src_id,
      NpuId dest_id,
      PayloadSize payload_size) noexcept = 0;

 protected:
  // functions that should be implemented
  /**
   * Translate NPU's Id (int form) to NPU's Address (ID per each dimension form)
   * @param id NPU's id
   * @return NPU's address
   */
  virtual NpuAddress npuIdToAddress(NpuId id) const noexcept = 0;

  /**
   * Translate NPU's Address (ID per each dimension form) to NPU's Id (int form)
   * @param address NPU's address
   * @return NPU's id
   */
  virtual NpuId npuAddressToId(const NpuAddress& address) const noexcept = 0;

  // members
  TopologyConfigurations
      configurations; // topology configuration for each dimension
  std::map<NpuId, std::map<NpuId, Link>>
      links; // links[src][dest] returns a link connecting two links
  int communication_bounds_count =
      0; // the number of occasions link_latency was larger
  int hbm_bounds_count = 0; // the number of occasions hbm_latency was larger

  // helper functions that are already implemented
  /**
   * Add a link connecting from src to dest.
   * @param src_id
   * @param dest_id
   * @param dimension dimension of the link
   */
  void connect(NpuId src_id, NpuId dest_id, int dimension) noexcept;

  /**
   * Send a packet from src to dest, and return the latency.
   * src and dest must be connected.
   * @param src_id
   * @param dest_id
   * @param payload_size
   * @return latency of src->dest transmission
   */
  Latency route(NpuId src_id, NpuId dest_id, PayloadSize payload_size) noexcept;

  /**
   * Simulate the serialization delay.
   * @param dimension dimension to use
   * @return serialization delay
   */
  Latency serialize(PayloadSize payload_size, int dimension) const noexcept;

  /**
   * Simulate router latency.
   * @param dimension dimension of the router
   * @return router latency
   */
  Latency routerLatency(int dimension) const noexcept;

  /**
   * Simulate nic latency.
   * @param dimension dimension of the nic
   * @return nic latency
   */
  Latency nicLatency(int dimension) const noexcept;

  /**
   * Simulate HBM latency.
   * @param payload_size
   * @param dimension dimension of the hbm
   * @return hbm latency
   */
  Latency hbmLatency(PayloadSize payload_size, int dimension) const noexcept;

  /**
   * Return the largest latency among overlapped operations.
   * @param link_latency
   * @param hbm_latency
   * @return larger latency
   */
  Latency criticalLatency(Latency link_latency, Latency hbm_latency) noexcept;
};
} // namespace Analytical

#endif
