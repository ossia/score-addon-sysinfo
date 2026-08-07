#include "Platform.hpp"

#include <ossia/detail/dylib_loader.hpp>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off
#include <iphlpapi.h>
// clang-format on
#else
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <ifaddrs.h>
#include <libproc.h>
#include <net/if.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

namespace SysInfo::platform
{
namespace
{
std::optional<std::string> read_line(const std::filesystem::path& path)
{
  std::ifstream f{path};
  if(!f)
    return std::nullopt;

  std::string line;
  if(!std::getline(f, line))
    return std::nullopt;

  while(!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
    line.pop_back();
  return line;
}

std::uint64_t read_uint(const std::filesystem::path& path)
{
  if(const auto line = read_line(path))
  {
    try
    {
      return std::stoull(*line);
    }
    catch(...)
    {
    }
  }
  return 0;
}
}

filesystem_stats mount_stats(const std::string& path)
{
#if defined(_WIN32)
  ULARGE_INTEGER available{}, total{};
  if(GetDiskFreeSpaceExA(path.c_str(), &available, nullptr, &total))
    return {total.QuadPart, available.QuadPart};
#else
  struct statvfs st
  {
  };
  if(::statvfs(path.c_str(), &st) == 0)
    return {
        std::uint64_t(st.f_blocks) * st.f_frsize,
        std::uint64_t(st.f_bavail) * st.f_frsize};
#endif
  return {};
}

#if defined(__linux__)
namespace
{
//! /proc/self/mounts octal-escapes space, tab, newline and backslash
std::string unescape_mount_path(const std::string& in)
{
  std::string out;
  out.reserve(in.size());
  for(std::size_t i = 0; i < in.size(); i++)
  {
    if(in[i] == '\\' && i + 3 < in.size() && in[i + 1] >= '0' && in[i + 1] <= '7'
       && in[i + 2] >= '0' && in[i + 2] <= '7' && in[i + 3] >= '0' && in[i + 3] <= '7')
    {
      out.push_back(
          char((in[i + 1] - '0') * 64 + (in[i + 2] - '0') * 8 + (in[i + 3] - '0')));
      i += 3;
    }
    else
    {
      out.push_back(in[i]);
    }
  }
  return out;
}
}

std::unordered_map<std::string, std::string> mount_table()
{
  std::unordered_map<std::string, std::string> table;

  std::ifstream f{"/proc/self/mounts"};
  std::string device, path, rest;
  while(f >> device >> path)
  {
    std::getline(f, rest);
    // Only the first mount of a device is kept: the next ones are bind mounts
    // of the same filesystem and would be counted twice.
    if(device.rfind("/dev/", 0) == 0)
      table.emplace(device, unescape_mount_path(path));
  }

  return table;
}
#else
std::unordered_map<std::string, std::string> mount_table()
{
  return {};
}
#endif

load_average loadavg()
{
#if defined(_WIN32)
  return {};
#else
  double avg[3]{};
  if(::getloadavg(avg, 3) != 3)
    return {};
  return {float(avg[0]), float(avg[1]), float(avg[2])};
#endif
}

double uptime()
{
#if defined(_WIN32)
  return double(GetTickCount64()) / 1000.;
#elif defined(__linux__)
  std::ifstream f{"/proc/uptime"};
  double seconds{};
  if(f >> seconds)
    return seconds;
  return 0.;
#elif defined(__APPLE__)
  struct timeval boot
  {
  };
  std::size_t len = sizeof(boot);
  int mib[2]{CTL_KERN, KERN_BOOTTIME};
  if(::sysctl(mib, 2, &boot, &len, nullptr, 0) != 0)
    return 0.;

  struct timeval now
  {
  };
  ::gettimeofday(&now, nullptr);
  return double(now.tv_sec - boot.tv_sec) + 1e-6 * double(now.tv_usec - boot.tv_usec);
#else
  return 0.;
#endif
}

file_handles open_files()
{
  file_handles res;

#if defined(_WIN32)
  DWORD handles{};
  if(GetProcessHandleCount(GetCurrentProcess(), &handles))
    res.process_open = handles;
#else
  struct rlimit lim
  {
  };
  if(::getrlimit(RLIMIT_NOFILE, &lim) == 0 && lim.rlim_cur != RLIM_INFINITY)
    res.process_max = lim.rlim_cur;
#endif

#if defined(__linux__)
  std::error_code ec;
  const auto n = std::distance(
      std::filesystem::directory_iterator{"/proc/self/fd", ec},
      std::filesystem::directory_iterator{});
  // The iterator holds one of the descriptors it is listing
  res.process_open = n > 0 ? n - 1 : 0;

  // "allocated  free  max", where free has been 0 since Linux 2.6
  std::ifstream f{"/proc/sys/fs/file-nr"};
  std::uint64_t allocated{}, unused{}, max{};
  if(f >> allocated >> unused >> max)
  {
    res.system_open = allocated - std::min(allocated, unused);
    res.system_max = max;
  }
#elif defined(__APPLE__)
  const int bytes = ::proc_pidinfo(::getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
  if(bytes > 0)
    res.process_open = std::uint64_t(bytes) / sizeof(struct proc_fdinfo);

  auto sysctl_uint = [](const char* name) -> std::uint64_t {
    int value{};
    std::size_t len = sizeof(value);
    if(::sysctlbyname(name, &value, &len, nullptr, 0) == 0 && value > 0)
      return std::uint64_t(value);
    return 0;
  };
  res.system_open = sysctl_uint("kern.num_files");
  res.system_max = sysctl_uint("kern.maxfiles");
#endif

  return res;
}

std::unordered_map<std::string, network_counters> network_traffic()
{
  std::unordered_map<std::string, network_counters> res;

#if defined(__linux__)
  std::error_code ec;
  for(const auto& entry : std::filesystem::directory_iterator("/sys/class/net", ec))
  {
    const auto stats = entry.path() / "statistics";
    res[entry.path().filename().string()] = network_counters{
        .rx = read_uint(stats / "rx_bytes"), .tx = read_uint(stats / "tx_bytes")};
  }
#elif defined(__APPLE__)
  struct ifaddrs* addrs{};
  if(::getifaddrs(&addrs) != 0)
    return res;

  for(auto* it = addrs; it; it = it->ifa_next)
  {
    if(!it->ifa_addr || it->ifa_addr->sa_family != AF_LINK || !it->ifa_data)
      continue;

    const auto* data = reinterpret_cast<const struct if_data*>(it->ifa_data);
    res[it->ifa_name]
        = network_counters{.rx = data->ifi_ibytes, .tx = data->ifi_obytes};
  }
  ::freeifaddrs(addrs);
#elif defined(_WIN32)
  // hwinfo reports the interface index rather than a name on Windows
  MIB_IF_TABLE2* table{};
  if(GetIfTable2(&table) != NO_ERROR || !table)
    return res;

  for(ULONG i = 0; i < table->NumEntries; i++)
  {
    const auto& row = table->Table[i];
    res[std::to_string(row.InterfaceIndex)] = network_counters{
        .rx = row.InOctets, .tx = row.OutOctets};
  }
  FreeMibTable(table);
#endif

  return res;
}

// ---- GPU -------------------------------------------------------------------

namespace
{
using nvmlDevice_t = void*;

struct nvmlMemory_t
{
  unsigned long long total, free, used;
};

struct nvmlUtilization_t
{
  unsigned int gpu, memory;
};

//! Minimal binding to the NVIDIA management library, loaded if the driver is
//! installed. It is the only way to get memory, clock and load for an NVIDIA
//! GPU: the kernel driver exposes nothing usable in sysfs.
struct nvml_api
{
  int (*init)(){};
  int (*shutdown)(){};
  int (*handle_by_pci)(const char*, nvmlDevice_t*){};
  int (*memory_info)(nvmlDevice_t, nvmlMemory_t*){};
  int (*utilization)(nvmlDevice_t, nvmlUtilization_t*){};
  int (*clock_info)(nvmlDevice_t, int, unsigned int*){};
  int (*temperature)(nvmlDevice_t, int, unsigned int*){};
  int (*driver_version)(char*, unsigned int){};
  int (*num_cores)(nvmlDevice_t, unsigned int*){};

  static const nvml_api* instance() noexcept
  {
    static const std::unique_ptr<nvml_api> self = []() -> std::unique_ptr<nvml_api> {
      try
      {
        return std::unique_ptr<nvml_api>(new nvml_api);
      }
      catch(...)
      {
        return {};
      }
    }();
    return self.get();
  }

private:
  nvml_api()
      : library{{"libnvidia-ml.so.1", "libnvidia-ml.so", "nvml.dll"}}
  {
    init = library.symbol<decltype(init)>("nvmlInit_v2");
    shutdown = library.symbol<decltype(shutdown)>("nvmlShutdown");
    handle_by_pci = library.symbol<decltype(handle_by_pci)>(
        "nvmlDeviceGetHandleByPciBusId_v2");
    memory_info = library.symbol<decltype(memory_info)>("nvmlDeviceGetMemoryInfo");
    utilization
        = library.symbol<decltype(utilization)>("nvmlDeviceGetUtilizationRates");
    clock_info = library.symbol<decltype(clock_info)>("nvmlDeviceGetClockInfo");
    temperature = library.symbol<decltype(temperature)>("nvmlDeviceGetTemperature");
    driver_version
        = library.symbol<decltype(driver_version)>("nvmlSystemGetDriverVersion");
    // Only in the R510 drivers and later
    num_cores = library.symbol<decltype(num_cores)>("nvmlDeviceGetNumGpuCores");

    if(!init || !handle_by_pci || !memory_info)
      throw std::runtime_error("nvml: incomplete");
    if(init() != 0)
      throw std::runtime_error("nvml: cannot initialize");
  }

  ossia::dylib_loader library;
};

struct gpu_source
{
  std::uint32_t id{};
  std::filesystem::path card;
  std::filesystem::path device;
  std::filesystem::path hwmon;
  nvmlDevice_t nvml{};
};

std::mutex g_gpu_mutex;
std::vector<gpu_source> g_gpu_sources;

#if defined(__linux__)
//! PCI_SLOT_NAME=0000:01:00.0 in the DRM device uevent, which is the address
//! NVML expects
std::string read_pci_address(const std::filesystem::path& device)
{
  std::ifstream f{device / "uevent"};
  std::string line;
  while(std::getline(f, line))
  {
    constexpr std::string_view key = "PCI_SLOT_NAME=";
    if(line.rfind(key, 0) == 0)
      return line.substr(key.size());
  }
  return {};
}

std::filesystem::path find_hwmon(const std::filesystem::path& device)
{
  std::error_code ec;
  for(const auto& entry : std::filesystem::directory_iterator(device / "hwmon", ec))
    return entry.path();
  return {};
}
#endif
}

void init_gpu_readings(const std::vector<std::uint32_t>& gpu_ids)
{
  std::vector<gpu_source> sources;

#if defined(__linux__)
  const auto* nvml = nvml_api::instance();

  for(auto id : gpu_ids)
  {
    gpu_source src;
    src.id = id;
    src.card = "/sys/class/drm/card" + std::to_string(id);
    src.device = src.card / "device";

    std::error_code ec;
    if(!std::filesystem::exists(src.device, ec))
      continue;

    src.hwmon = find_hwmon(src.device);

    if(nvml)
    {
      if(const auto pci = read_pci_address(src.device); !pci.empty())
      {
        nvmlDevice_t dev{};
        if(nvml->handle_by_pci(pci.c_str(), &dev) == 0)
          src.nvml = dev;
      }
    }

    sources.push_back(std::move(src));
  }
#else
  (void)gpu_ids;
#endif

  std::lock_guard lock{g_gpu_mutex};
  g_gpu_sources = std::move(sources);
}

std::unordered_map<std::uint32_t, gpu_description> gpu_descriptions()
{
  std::unordered_map<std::uint32_t, gpu_description> res;

  std::vector<gpu_source> sources;
  {
    std::lock_guard lock{g_gpu_mutex};
    sources = g_gpu_sources;
  }

  if(sources.empty())
    return res;

  const auto* nvml = nvml_api::instance();
  if(!nvml)
    return res;

  std::string driver;
  if(nvml && nvml->driver_version)
  {
    char buf[128]{};
    if(nvml->driver_version(buf, sizeof(buf)) == 0)
      driver = buf;
  }

  for(const auto& src : sources)
  {
    if(!src.nvml)
      continue;

    gpu_description d;
    d.driver = driver;

    unsigned int cores{};
    if(nvml->num_cores && nvml->num_cores(src.nvml, &cores) == 0)
      d.cores = cores;

    res[src.id] = std::move(d);
  }

  return res;
}

std::unordered_map<std::uint32_t, gpu_metrics> gpu_readings()
{
  std::unordered_map<std::uint32_t, gpu_metrics> res;

  std::vector<gpu_source> sources;
  {
    std::lock_guard lock{g_gpu_mutex};
    sources = g_gpu_sources;
  }

#if defined(__linux__)
  const auto* nvml = nvml_api::instance();

  for(const auto& src : sources)
  {
    gpu_metrics m;

    // amdgpu
    m.memory_total = read_uint(src.device / "mem_info_vram_total");
    m.memory_used = read_uint(src.device / "mem_info_vram_used");
    if(const auto busy = read_line(src.device / "gpu_busy_percent"))
    {
      try
      {
        m.utilization = std::stof(*busy) / 100.f;
      }
      catch(...)
      {
      }
    }
    if(!src.hwmon.empty())
    {
      m.clock_hz = read_uint(src.hwmon / "freq1_input");
      if(const auto temp = read_uint(src.hwmon / "temp1_input"); temp > 0)
        m.temperature = float(temp) / 1000.f;
    }

    // i915 reports its clock in MHz next to the card rather than the device
    if(m.clock_hz == 0)
      m.clock_hz = read_uint(src.card / "gt_cur_freq_mhz") * 1'000'000ull;

    if(nvml && src.nvml)
    {
      nvmlMemory_t mem{};
      if(nvml->memory_info(src.nvml, &mem) == 0)
      {
        m.memory_total = mem.total;
        m.memory_used = mem.used;
      }

      nvmlUtilization_t util{};
      if(nvml->utilization && nvml->utilization(src.nvml, &util) == 0)
        m.utilization = float(util.gpu) / 100.f;

      unsigned int clock{};
      if(nvml->clock_info && nvml->clock_info(src.nvml, 0, &clock) == 0)
        m.clock_hz = std::uint64_t(clock) * 1'000'000ull;

      unsigned int temp{};
      if(nvml->temperature && nvml->temperature(src.nvml, 0, &temp) == 0)
        m.temperature = float(temp);
    }

    res[src.id] = m;
  }
#endif

  return res;
}
}
