#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Readings that hwinfo does not provide, or provides in a form that is unusable
 * as-is. Each function returns zeroed data rather than failing when the
 * platform has no equivalent, so the address tree stays the same everywhere.
 */
namespace SysInfo::platform
{
struct filesystem_stats
{
  std::uint64_t total{};
  std::uint64_t free{};
};

//! Capacity and free space of the filesystem mounted at @p path
filesystem_stats mount_stats(const std::string& path);

/**
 * Partition device node -> path it is mounted on.
 *
 * hwinfo reports device nodes as the mount points of a disk on Linux, and
 * statvfs'ing those measures /dev rather than the filesystem. Empty on the
 * platforms where hwinfo already reports paths.
 */
std::unordered_map<std::string, std::string> mount_table();

struct load_average
{
  float one{};
  float five{};
  float fifteen{};
};

//! Zeroed on Windows, which has no equivalent
load_average loadavg();

//! Seconds since boot, 0 if unknown
double uptime();

struct network_counters
{
  std::uint64_t rx{};
  std::uint64_t tx{};
};

//! Cumulative byte counters, keyed by interface name
std::unordered_map<std::string, network_counters> network_traffic();

struct gpu_metrics
{
  std::uint64_t memory_total{};
  std::uint64_t memory_used{};
  std::uint64_t clock_hz{};
  //! [0; 1], negative when the platform does not report it
  float utilization{-1.f};
  //! Degrees Celsius, negative when the platform does not report it
  float temperature{-1.f};
};

/**
 * Keyed by the id hwinfo assigns to a GPU, which on Linux is the DRM card
 * number. Filled from the amdgpu / i915 sysfs nodes and, when the driver is
 * loaded, from NVML.
 */
std::unordered_map<std::uint32_t, gpu_metrics> gpu_readings();

struct gpu_description
{
  std::string driver;
  std::uint64_t cores{};
};

//! What the vendor libraries know that hwinfo does not, keyed like gpu_readings
std::unordered_map<std::uint32_t, gpu_description> gpu_descriptions();

//! Called once before the first gpu_readings(); safe to call again
void init_gpu_readings(const std::vector<std::uint32_t>& gpu_ids);
}
