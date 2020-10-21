/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include "api/AnalyticalNetwork.hh"
#include "astra-sim/system/Sys.hh"
#include "astra-sim/system/memory/SimpleMemory.hh"
#include "event-queue/EventQueue.hh"
#include "event-queue/EventQueueEntry.hh"
#include "helper/CommandLineParser.hh"
#include "helper/json.hh"
#include "topology/AllToAll.hh"
#include "topology/Ring.hh"
#include "topology/Switch.hh"
#include "topology/Topology.hh"
#include "topology/TopologyConfiguration.hh"
#include "topology/Torus2D.hh"

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
  /**
   * Configuration parsing
   */
  auto cmd_parser = Analytical::CommandLineParser();

  // Define command line arguments here
  cmd_parser.add_command_line_option<std::string>(
      "network-configuration", "Network configuration file");
  cmd_parser.add_command_line_option<std::string>(
      "system-configuration", "System configuration file");
  cmd_parser.add_command_line_option<std::string>(
      "workload-configuration", "Workload configuration file");
  cmd_parser.add_command_line_option<int>(
      "num-passes", "Number of passes to run");
  cmd_parser.add_command_line_option<int>(
      "num-queues-per-dim", "Number of queues per each dimension");
  cmd_parser.add_command_line_option<float>(
      "comm-scale", "Communication scale");
  cmd_parser.add_command_line_option<float>("compute-scale", "Compute scale");
  cmd_parser.add_command_line_option<float>(
      "injection-scale", "Injection scale");
  cmd_parser.add_command_line_option<std::string>(
      "path", "Path to save result files");
  cmd_parser.add_command_line_option<std::string>("run-name", "Run name");
  cmd_parser.add_command_line_option<int>(
      "total-stat-rows", "Total number of concurrent runs");
  cmd_parser.add_command_line_option<int>(
      "stat-row", "Index of current run (index starts with 0)");
  cmd_parser.add_command_line_option<bool>(
      "rendezvous-protocol", "Whether to enable rendezvous protocol");

  // Parse command line arguments
  try {
    cmd_parser.parse(argc, argv);
  } catch (const Analytical::CommandLineParser::ParsingError& e) {
    std::cout << e.what() << std::endl;
    exit(-1);
  }

  cmd_parser.print_help_message_if_required();

  // 1. Retrieve network-agnostic configs
  std::string system_configuration = "system path not defined";
  cmd_parser.set_if_defined("system-configuration", &system_configuration);

  std::string workload_configuration = "workload path not defined";
  cmd_parser.set_if_defined("workload-configuration", &workload_configuration);

  int num_passes = 1;
  cmd_parser.set_if_defined("num-passes", &num_passes);

  int num_queues_per_dim = 1;
  cmd_parser.set_if_defined("num-queues-per-dim", &num_queues_per_dim);

  float comm_scale = 1;
  cmd_parser.set_if_defined("comm-scale", &comm_scale);

  float compute_scale = 1;
  cmd_parser.set_if_defined("compute-scale", &compute_scale);

  float injection_scale = 1;
  cmd_parser.set_if_defined("injection-scale", &injection_scale);

  std::string path = "path not defined";
  cmd_parser.set_if_defined("path", &path);

  std::string run_name = "unnamed run";
  cmd_parser.set_if_defined("run-name", &run_name);

  int total_stat_rows = 1;
  cmd_parser.set_if_defined("total-stat-rows", &total_stat_rows);

  int stat_row = 0;
  cmd_parser.set_if_defined("stat-row", &stat_row);

  bool rendezvous_protocol = false;
  cmd_parser.set_if_defined("rendezvous-protocol", &rendezvous_protocol);

  // 2. Retrieve network configs
  std::string network_configuration = ""; // default configuration.json
  cmd_parser.set_if_defined("network-configuration", &network_configuration);
  if (network_configuration.empty()) {
    std::cout
        << "[Analytical, function main] Network configuration file path not given!"
        << std::endl;
    exit(-1);
  }

  // parse configuration.json file
  auto json_file = std::ifstream(network_configuration, std::ifstream::in);
  if (!json_file) {
    std::cout << "[Analytical] Failed to open network configuration file at: "
              << network_configuration << std::endl;
    exit(-1);
  }

  nlohmann::json json_configuration;
  json_file >> json_configuration;
  json_file.close();

  std::string topology_name = json_configuration["topology-name"];

  int dimensions_count = json_configuration["dimensions-count"];

  std::vector<int> packages_counts;
  for (int node_per_dim : json_configuration["packages-counts"]) {
    packages_counts.emplace_back(node_per_dim);
  }

  std::vector<std::vector<int>> topology_shape_configs;
  for (const auto& topology_shape_config :
       json_configuration["topology-shape-configs"]) {
    auto& configs = topology_shape_configs.emplace_back(std::vector<int>());
    for (const auto a_config : topology_shape_config) {
      configs.emplace_back(a_config);
    }
  }

  std::vector<double> link_bandwidths;
  for (double link_bandwidth : json_configuration["link-bandwidth"]) {
    link_bandwidths.emplace_back(link_bandwidth);
  }

  std::vector<double> link_latencies;
  for (double link_latency : json_configuration["link-latency"]) {
    link_latencies.emplace_back(link_latency);
  }

  std::vector<double> nic_latencies;
  for (double nic_latency : json_configuration["nic-latency"]) {
    nic_latencies.emplace_back(nic_latency);
  }

  std::vector<double> router_latencies;
  for (double router_latency : json_configuration["router-latency"]) {
    router_latencies.emplace_back(router_latency);
  }

  std::vector<double> hbm_bandwidths;
  for (double hbm_bandwidth : json_configuration["hbm-bandwidth"]) {
    hbm_bandwidths.emplace_back(hbm_bandwidth);
  }

  std::vector<double> hbm_latencies;
  for (double hbm_latency : json_configuration["hbm-latency"]) {
    hbm_latencies.emplace_back(hbm_latency);
  }

  std::vector<double> hbm_scales;
  for (double hbm_scale : json_configuration["hbm-scale"]) {
    hbm_scales.emplace_back(hbm_scale);
  }

  /**
   * Instantitiation: Event Queue, System, Memory, Topology, etc.
   */
  // event queue instantiation
  auto event_queue = std::make_shared<Analytical::EventQueue>();

  // compute total number of npus by multiplying counts of each dimension
  auto total_npus_count = 1;
  for (auto node_per_dim : packages_counts) {
    total_npus_count *= node_per_dim;
  }

  // number of nodes for each system layer dimension
  auto nodes_count_for_system = std::array<int, 5>();
  for (int i = 0; i < 4; i++) {
    nodes_count_for_system[i] = 1;
  }

  // Network and System layer initialization
  std::unique_ptr<Analytical::AnalyticalNetwork>
      analytical_networks[total_npus_count];
  AstraSim::Sys* systems[total_npus_count];
  std::unique_ptr<AstraSim::SimpleMemory> memories[total_npus_count];

  // pointer to topology
  std::shared_ptr<Analytical::Topology> topology;

  // topology configuration for each dimension
  auto topology_configurations =
      std::vector<Analytical::TopologyConfiguration>();
  for (int i = 0; i < dimensions_count; i++) {
    topology_configurations.emplace_back(
        packages_counts[i], // packages count
        topology_shape_configs[i], // topology shape configs
        link_latencies[i], // link latency (ns)
        link_bandwidths[i], // link bandwidth (GB/s) = (B/ns)
        nic_latencies[i], // nic latency (ns)
        router_latencies[i], // router latency (ns)
        hbm_latencies[i], // memory latency (ns),
        hbm_bandwidths[i], // memory bandwidth (GB/s) = (B/ns)
        hbm_scales[i] // memory scaling factor
    );
  }

  // Instantiate topology
  if (topology_name == "Switch") {
    topology = std::make_shared<Analytical::Switch>(topology_configurations);
    nodes_count_for_system[2] = total_npus_count;
  } else if (topology_name == "AllToAll") {
    topology = std::make_shared<Analytical::AllToAll>(topology_configurations);
    nodes_count_for_system[2] = total_npus_count;
  } else if (topology_name == "Torus2D") {
    topology = std::make_shared<Analytical::Torus2D>(topology_configurations);
    // TODO: check system input dimension
    nodes_count_for_system[1] = topology_shape_configs[0][0];
    nodes_count_for_system[2] = topology_shape_configs[0][1];
  } else if (topology_name == "Ring") {
    topology = std::make_shared<Analytical::Ring>(topology_configurations);
    nodes_count_for_system[2] = total_npus_count;
  } else {
    std::cout << "[Main] Topology not defined: " << topology_name << std::endl;
    exit(-1);
  }

  // Instantiate required network, memory, and system layers
  for (int i = 0; i < total_npus_count; i++) {
    analytical_networks[i] = std::make_unique<Analytical::AnalyticalNetwork>(i);

    memories[i] = std::make_unique<AstraSim::SimpleMemory>(
        (AstraSim::AstraNetworkAPI*)(analytical_networks[i].get()),
        500,
        270,
        12.5);

    systems[i] = new AstraSim::Sys(
        analytical_networks[i].get(), // AstraNetworkAPI
        memories[i].get(), // AstraMemoryAPI
        i, // id
        num_passes, // num_passes
        nodes_count_for_system[0],
        nodes_count_for_system[1],
        nodes_count_for_system[2],
        nodes_count_for_system[3],
        nodes_count_for_system[4], // dimensions
        num_queues_per_dim,
        num_queues_per_dim,
        num_queues_per_dim,
        num_queues_per_dim,
        num_queues_per_dim, // queues per corresponding dimension
        system_configuration, // system configuration
        workload_configuration, // workload configuration
        comm_scale,
        compute_scale,
        injection_scale, // communication, computation, injection scale
        total_stat_rows,
        stat_row, // total_stat_rows and stat_row
        path, // stat file path
        run_name, // run name
        true, // separate_log
        rendezvous_protocol // randezvous protocol
    );
  }

  // link event queue and topology
  Analytical::AnalyticalNetwork::set_event_queue(event_queue);
  Analytical::AnalyticalNetwork::set_topology(topology);

  /**
   * Run Analytical Model
   */
  // Initialize event queue
  for (int i = 0; i < total_npus_count; i++) {
    systems[i]->workload->fire();
  }

  // Run events
  while (!event_queue->empty()) {
    event_queue->proceed();
  }

  /**
   * Cleanup
   */
  // System class automatically deletes itself, so no need to free systems[i]
  // here. Invoking `free systems[i]` here will trigger segfault (by trying to
  // delete already deleted memory space)

  // terminate program
  return 0;
}
