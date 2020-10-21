/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "Torus2D.hh"
#include <cassert>
#include <cmath>
#include <iostream>
#include <tuple>
#include <utility>

using namespace Analytical;

Torus2D::Torus2D(const TopologyConfigurations& configurations) noexcept {
  this->configurations = configurations;

  auto& topology_shape_configs = configurations[0].getTopologyShapeConfigs();
  width = topology_shape_configs[0];
  height = topology_shape_configs[1];

  assert(
      width * height == configurations[0].getPackagesCount() &&
      "[Torus2D, constructor] Packages_count and (width * height) mismatches");

  // connect each row
  for (int row = 0; row < height; row++) {
    // iterate over NPUs, except for last column
    for (int col = 0; col < (width - 1); col++) {
      auto n1 = (row * width) + col;
      auto n2 = n1 + 1; // node on right
      connect(n1, n2, 0);
      connect(n2, n1, 0);
    }

    // last column NPU
    auto n1 = (row * width) + (width - 1);
    auto n2 = row * width;
    connect(n1, n2, 0);
    connect(n2, n1, 0);
  }

  // connect each column
  for (int col = 0; col < width; col++) {
    // iterate over NPUs, except for top row
    for (int row = 0; row < (height - 1); row++) {
      auto n1 = (row * width) + col;
      auto n2 = n1 + width; // node on top
      connect(n1, n2, 0);
      connect(n2, n1, 0);
    }

    // top column NPU
    auto n1 = (height - 1) * width + col;
    auto n2 = col;
    connect(n1, n2, 0);
    connect(n2, n1, 0);
  }
}

Topology::Latency Torus2D::send(
    NpuId src_id,
    NpuId dest_id,
    PayloadSize payload_size) noexcept {
  if (src_id == dest_id) {
    // guard statement
    return 0;
  }

  auto src_row = -1;
  auto src_col = -1;
  std::tie(src_row, src_col) = idToRowCol(src_id);

  auto dest_row = -1;
  auto dest_col = -1;
  std::tie(dest_row, dest_col) = idToRowCol(dest_id);

  // serialize
  auto link_latency = serialize(payload_size, 0);
  link_latency += nicLatency(0);

  // xy routing

  if (src_col != dest_col) {
    // should move x direction (i.e., move within row)
    auto direction = computeDirection(src_col, dest_col, width);

    auto current_col = src_col;
    while (current_col != dest_col) {
      auto next_col = takeStep(current_col, direction, width);

      auto current_id = rowColToId(src_row, current_col);
      auto next_id = rowColToId(src_row, next_col);
      link_latency += route(current_id, next_id, payload_size);

      current_col = next_col;
    }
  }

  if (src_row != dest_row) {
    // should move y direction (i.e., move within column)
    // should move x direction (i.e., move within row)
    auto direction = computeDirection(src_row, dest_row, height);

    auto current_row = src_row;
    while (current_row != dest_row) {
      auto next_row = takeStep(current_row, direction, height);

      auto current_id = rowColToId(current_row, dest_col);
      auto next_id = rowColToId(next_row, dest_col);
      link_latency += route(current_id, next_id, payload_size);

      current_row = next_row;
    }
  }

  link_latency += nicLatency(0);

  auto hbm_latency = hbmLatency(payload_size, 0);

  return criticalLatency(link_latency, hbm_latency);
}

Topology::NpuAddress Torus2D::npuIdToAddress(NpuId id) const noexcept {
  return NpuAddress(1, id);
}

Topology::NpuId Torus2D::npuAddressToId(
    const NpuAddress& address) const noexcept {
  return address[0];
}

Torus2D::Direction Torus2D::computeDirection(
    NpuId src_index,
    NpuId dest_index,
    int ring_size) const noexcept {
  auto half_ring_size = ring_size / 2;

  // bidirectional: compute shortest path
  if (src_index < dest_index) {
    auto distance = dest_index - src_index;
    return (distance <= half_ring_size) ? 1 : -1;
  }

  auto distance = src_index - dest_index;
  return (distance <= half_ring_size) ? -1 : 1;
}

std::pair<int, int> Torus2D::idToRowCol(NpuId id) const noexcept {
  auto row = id / width;
  auto col = id % width;
  return std::make_pair(row, col);
}

Torus2D::NpuId Torus2D::rowColToId(int row, int column) const noexcept {
  return (row * width) + column;
}

int Torus2D::takeStep(int current_index, Direction direction, int ring_size)
    const noexcept {
  // compute next id of the ring
  auto next_index = current_index + direction;

  if (next_index >= ring_size) {
    // out of positive bounds
    next_index %= ring_size;
  } else if (next_index < 0) {
    // out of negative bounds
    next_index = ring_size + (next_index % ring_size);
  }

  return next_index;
}
