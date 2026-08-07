#pragma once
#include <SysInfo/Platform.hpp>
#include <SysInfo/SpecificSettings.hpp>

#include <hwinfo/battery.h>
#include <hwinfo/cpu.h>
#include <hwinfo/disk.h>
#include <hwinfo/gpu.h>
#include <hwinfo/mainboard.h>
#include <hwinfo/network.h>
#include <hwinfo/os.h>
#include <hwinfo/ram.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ossia::net
{
class node_base;
class parameter_base;
}

namespace SysInfo
{
/**
 * @brief What Qt knows about the machine that hwinfo does not.
 *
 * Read on the UI thread, when the device connects: QScreen may not be touched
 * from anywhere else.
 */
struct qt_info
{
  struct display
  {
    std::string name;
    std::string manufacturer;
    std::string model;
    std::string serial;
    int x{}, y{}, width{}, height{};
    int available_width{}, available_height{};
    float refresh_rate{};
    float logical_dpi{};
    float physical_dpi{};
    float scale{};
    int depth{};
    float physical_width_mm{};
    float physical_height_mm{};
    std::string orientation;
    bool primary{};
  };

  std::string version;
  std::string platform;
  std::string style;

  std::string product;
  std::string architecture;
  std::string build_abi;

  std::vector<display> displays;

  static qt_info scan();
};

/**
 * @brief The parts of the machine description that never change while the
 * device is connected. Read once, when the device is created.
 */
struct hardware
{
  struct mount_point
  {
    int disk{};
    std::string path;
  };

  hwinfo::OS os;
  hwinfo::MainBoard mainboard;
  hwinfo::Memory memory;
  std::vector<hwinfo::CPU> cpus;
  std::vector<hwinfo::GPU> gpus;
  std::vector<hwinfo::Disk> disks;
  std::vector<hwinfo::Battery> batteries;
  std::vector<hwinfo::Network> networks;

  qt_info qt;

  //! What the vendor libraries add to hwinfo's GPU description
  std::unordered_map<std::uint32_t, platform::gpu_description> gpu_info;

  /**
   * (disk index, mounted path) pairs, flattened over all the disks.
   *
   * hwinfo reports partition device nodes on Linux and mounted paths on macOS
   * and Windows; these are the paths, resolved through the mount table, and
   * deduplicated so that a bind mount is not counted twice.
   */
  std::vector<mount_point> mounts;

  static hardware scan();
};

/**
 * @brief The parts that change over time. Read periodically on a worker thread
 * as some of the readings block (CPU load is a delta over a sampling window,
 * free disk space may sit on a network mount...).
 */
struct snapshot
{
  struct battery_state
  {
    float level{};
    float energy_now{};
    bool charging{};
    std::string state;
  };

  struct mount_state
  {
    std::uint64_t total{};
    std::uint64_t free{};
  };

  struct network_state
  {
    std::uint64_t rx_total{};
    std::uint64_t tx_total{};
    float rx{};
    float tx{};
  };

  float cpu_usage{};
  std::vector<float> thread_usage;
  std::vector<float> thread_frequency;

  std::uint64_t memory_free{};
  std::uint64_t memory_available{};

  //! Indexed like hardware::mounts
  std::vector<mount_state> mounts;

  //! Indexed like hardware::batteries
  std::vector<battery_state> batteries;

  //! Indexed like hardware::networks
  std::vector<network_state> networks;

  //! Indexed like hardware::gpus
  std::vector<platform::gpu_metrics> gpus;

  platform::load_average load;
  double uptime{};
};

/**
 * @brief Produces snapshots. Keeps the previous network byte counters around to
 * turn them into a throughput. Used from the worker thread only.
 */
class sampler
{
public:
  snapshot fetch(const hardware& hw, std::chrono::milliseconds cpu_window);

private:
  std::vector<std::uint64_t> m_prev_rx;
  std::vector<std::uint64_t> m_prev_tx;
  std::chrono::steady_clock::time_point m_prev_time{};
};

/**
 * @brief The ossia parameters mirroring the machine.
 *
 * The layout is identical on every operating system: what a given platform
 * cannot report is published as an empty string or a zero, so that a score
 * written against a Linux machine still runs against the same addresses on
 * macOS and Windows.
 */
class tree
{
public:
  void setup(
      ossia::net::node_base& root, const hardware& hw, const SpecificSettings& set);

  void push_static(const hardware& hw);
  void push_dynamic(const hardware& hw, const snapshot& snap);
  void push_time();

private:
  struct cpu_params
  {
    ossia::net::parameter_base* vendor{};
    ossia::net::parameter_base* model{};
    ossia::net::parameter_base* physical_cores{};
    ossia::net::parameter_base* logical_cores{};
    ossia::net::parameter_base* base_frequency{};
    ossia::net::parameter_base* max_frequency{};
    ossia::net::parameter_base* l1d{};
    ossia::net::parameter_base* l1i{};
    ossia::net::parameter_base* l2{};
    ossia::net::parameter_base* l3{};
    ossia::net::parameter_base* flags{};
  };

  struct gpu_params
  {
    ossia::net::parameter_base* vendor{};
    ossia::net::parameter_base* name{};
    ossia::net::parameter_base* driver{};
    ossia::net::parameter_base* memory_total{};
    ossia::net::parameter_base* memory_used{};
    ossia::net::parameter_base* memory_shared{};
    ossia::net::parameter_base* memory_usage{};
    ossia::net::parameter_base* frequency{};
    ossia::net::parameter_base* usage{};
    ossia::net::parameter_base* temperature{};
    ossia::net::parameter_base* cores{};
    ossia::net::parameter_base* vendor_id{};
    ossia::net::parameter_base* device_id{};
  };

  struct memory_module_params
  {
    ossia::net::parameter_base* vendor{};
    ossia::net::parameter_base* name{};
    ossia::net::parameter_base* model{};
    ossia::net::parameter_base* serial{};
    ossia::net::parameter_base* size{};
    ossia::net::parameter_base* frequency{};
  };

  struct disk_params
  {
    ossia::net::parameter_base* vendor{};
    ossia::net::parameter_base* model{};
    ossia::net::parameter_base* serial{};
    ossia::net::parameter_base* size{};
    ossia::net::parameter_base* interface{};
    ossia::net::parameter_base* mountpoints{};
    ossia::net::parameter_base* total{};
    ossia::net::parameter_base* free{};
    ossia::net::parameter_base* usage{};
  };

  struct battery_params
  {
    ossia::net::parameter_base* vendor{};
    ossia::net::parameter_base* model{};
    ossia::net::parameter_base* serial{};
    ossia::net::parameter_base* technology{};
    ossia::net::parameter_base* level{};
    ossia::net::parameter_base* energy_now{};
    ossia::net::parameter_base* energy_full{};
    ossia::net::parameter_base* charging{};
    ossia::net::parameter_base* state{};
  };

  struct network_params
  {
    ossia::net::parameter_base* index{};
    ossia::net::parameter_base* description{};
    ossia::net::parameter_base* mac{};
    ossia::net::parameter_base* ipv4{};
    ossia::net::parameter_base* ipv6{};
    ossia::net::parameter_base* rx{};
    ossia::net::parameter_base* tx{};
    ossia::net::parameter_base* rx_total{};
    ossia::net::parameter_base* tx_total{};
  };

  struct display_params
  {
    ossia::net::parameter_base* name{};
    ossia::net::parameter_base* manufacturer{};
    ossia::net::parameter_base* model{};
    ossia::net::parameter_base* serial{};
    ossia::net::parameter_base* width{};
    ossia::net::parameter_base* height{};
    ossia::net::parameter_base* x{};
    ossia::net::parameter_base* y{};
    ossia::net::parameter_base* available_width{};
    ossia::net::parameter_base* available_height{};
    ossia::net::parameter_base* refresh_rate{};
    ossia::net::parameter_base* logical_dpi{};
    ossia::net::parameter_base* physical_dpi{};
    ossia::net::parameter_base* scale{};
    ossia::net::parameter_base* depth{};
    ossia::net::parameter_base* physical_width{};
    ossia::net::parameter_base* physical_height{};
    ossia::net::parameter_base* orientation{};
    ossia::net::parameter_base* primary{};
  };

  struct time_params
  {
    ossia::net::parameter_base* iso{};
    ossia::net::parameter_base* date{};
    ossia::net::parameter_base* clock{};
    ossia::net::parameter_base* year{};
    ossia::net::parameter_base* month{};
    ossia::net::parameter_base* day{};
    ossia::net::parameter_base* weekday{};
    ossia::net::parameter_base* hour{};
    ossia::net::parameter_base* minute{};
    ossia::net::parameter_base* second{};
    ossia::net::parameter_base* day_seconds{};
    ossia::net::parameter_base* unix_time{};
    ossia::net::parameter_base* timezone{};
    ossia::net::parameter_base* utc_offset{};
  };

  ossia::net::parameter_base* m_hostname{};
  ossia::net::parameter_base* m_uptime{};

  time_params m_time;

  ossia::net::parameter_base* m_load_1{};
  ossia::net::parameter_base* m_load_5{};
  ossia::net::parameter_base* m_load_15{};

  ossia::net::parameter_base* m_os_name{};
  ossia::net::parameter_base* m_os_product{};
  ossia::net::parameter_base* m_os_version{};
  ossia::net::parameter_base* m_os_kernel{};
  ossia::net::parameter_base* m_os_architecture{};
  ossia::net::parameter_base* m_os_bits{};
  ossia::net::parameter_base* m_os_endianness{};

  ossia::net::parameter_base* m_qt_version{};
  ossia::net::parameter_base* m_qt_platform{};
  ossia::net::parameter_base* m_qt_style{};
  ossia::net::parameter_base* m_qt_abi{};

  ossia::net::parameter_base* m_mainboard_vendor{};
  ossia::net::parameter_base* m_mainboard_name{};
  ossia::net::parameter_base* m_mainboard_version{};
  ossia::net::parameter_base* m_mainboard_serial{};

  ossia::net::parameter_base* m_cpu_count{};
  ossia::net::parameter_base* m_cpu_threads{};
  ossia::net::parameter_base* m_cpu_usage{};
  ossia::net::parameter_base* m_cpu_thread_usage{};
  ossia::net::parameter_base* m_cpu_thread_frequency{};
  std::vector<cpu_params> m_cpus;

  ossia::net::parameter_base* m_gpu_count{};
  std::vector<gpu_params> m_gpus;

  ossia::net::parameter_base* m_memory_total{};
  ossia::net::parameter_base* m_memory_free{};
  ossia::net::parameter_base* m_memory_available{};
  ossia::net::parameter_base* m_memory_used{};
  ossia::net::parameter_base* m_memory_usage{};
  ossia::net::parameter_base* m_memory_module_count{};
  std::vector<memory_module_params> m_memory_modules;

  ossia::net::parameter_base* m_disk_count{};
  ossia::net::parameter_base* m_disk_total{};
  ossia::net::parameter_base* m_disk_free{};
  ossia::net::parameter_base* m_disk_usage{};
  std::vector<disk_params> m_disks;

  ossia::net::parameter_base* m_battery_count{};
  ossia::net::parameter_base* m_battery_level{};
  ossia::net::parameter_base* m_battery_charging{};
  std::vector<battery_params> m_batteries;

  ossia::net::parameter_base* m_network_count{};
  ossia::net::parameter_base* m_network_rx{};
  ossia::net::parameter_base* m_network_tx{};
  std::vector<network_params> m_networks;

  ossia::net::parameter_base* m_display_count{};
  std::vector<display_params> m_displays;
};
}
