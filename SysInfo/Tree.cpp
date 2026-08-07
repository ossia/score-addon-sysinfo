#include "Tree.hpp"

#include <ossia/network/base/node.hpp>
#include <ossia/network/base/node_attributes.hpp>
#include <ossia/network/base/node_functions.hpp>
#include <ossia/network/base/parameter.hpp>
#include <ossia/network/domain/domain.hpp>

#include <hwinfo/monitoring/cpu.h>
#include <hwinfo/monitoring/ram.h>

#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <QTimeZone>

#include <QApplication>
#include <QStyle>

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace SysInfo
{
namespace
{
const char* to_string(hwinfo::Disk::Interface i) noexcept
{
  switch(i)
  {
    case hwinfo::Disk::Interface::NVME:
      return "NVMe";
    case hwinfo::Disk::Interface::USB:
      return "USB";
    case hwinfo::Disk::Interface::USB1:
      return "USB1";
    case hwinfo::Disk::Interface::USB2:
      return "USB2";
    case hwinfo::Disk::Interface::USB3_5GBit:
      return "USB3 (5 Gbit/s)";
    case hwinfo::Disk::Interface::USB3_10GBit:
      return "USB3 (10 Gbit/s)";
    case hwinfo::Disk::Interface::USB3_20GBit:
      return "USB3 (20 Gbit/s)";
    case hwinfo::Disk::Interface::USB4_20GBit:
      return "USB4 (20 Gbit/s)";
    case hwinfo::Disk::Interface::USB4_40GBit:
      return "USB4 (40 Gbit/s)";
    case hwinfo::Disk::Interface::USB4_80GBit:
      return "USB4 (80 Gbit/s)";
    case hwinfo::Disk::Interface::SATA:
      return "SATA";
    case hwinfo::Disk::Interface::SCSI:
      return "SCSI";
    case hwinfo::Disk::Interface::UNKNOWN:
    default:
      return "unknown";
  }
}

const char* to_string(hwinfo::Battery::State s) noexcept
{
  switch(s)
  {
    case hwinfo::Battery::State::CHARGING:
      return "charging";
    case hwinfo::Battery::State::DISCHARGING:
      return "discharging";
    case hwinfo::Battery::State::UNKNOWN:
    default:
      return "unknown";
  }
}

const char* to_string(Qt::ScreenOrientation o) noexcept
{
  switch(o)
  {
    case Qt::PortraitOrientation:
      return "portrait";
    case Qt::LandscapeOrientation:
      return "landscape";
    case Qt::InvertedPortraitOrientation:
      return "inverted portrait";
    case Qt::InvertedLandscapeOrientation:
      return "inverted landscape";
    case Qt::PrimaryOrientation:
    default:
      return "primary";
  }
}

ossia::net::parameter_base* addr(
    ossia::net::node_base& root, const std::string& path, ossia::val_type type,
    const char* desc)
{
  auto& n = ossia::net::create_node(root, path);
  auto* p = n.create_parameter(type);
  if(!p)
    return nullptr;

  p->set_access(ossia::access_mode::GET);
  ossia::net::set_description(n, desc);
  return p;
}

ossia::net::parameter_base*
ratio(ossia::net::node_base& root, const std::string& path, const char* desc)
{
  auto* p = addr(root, path, ossia::val_type::FLOAT, desc);
  if(p)
    p->set_domain(ossia::make_domain(0.f, 1.f));
  return p;
}

void push(ossia::net::parameter_base* p, ossia::value v)
{
  if(p)
    p->push_value(std::move(v));
}

//! Byte counts and frequencies do not fit in ossia's 32-bit integers, so they
//! are published as floats. 32 GiB is then accurate to ~4 kB, which is plenty
//! for a monitoring device.
ossia::value quantity(std::uint64_t v) noexcept
{
  return float(v);
}

//! Linux exposes the cpufreq sysfs values in kHz, which hwinfo forwards as if
//! they were Hz. Nothing score runs on is clocked below 10 MHz.
ossia::value frequency(std::uint64_t hz) noexcept
{
  if(hz > 0 && hz < 10'000'000)
    hz *= 1000;
  return float(hz);
}

ossia::value strings(const std::vector<std::string>& v)
{
  std::vector<ossia::value> res;
  res.reserve(v.size());
  for(const auto& s : v)
    res.push_back(s);
  return res;
}

ossia::value floats(const std::vector<float>& v)
{
  std::vector<ossia::value> res;
  res.reserve(v.size());
  for(float f : v)
    res.push_back(f);
  return res;
}

std::string idx(std::string_view prefix, std::size_t i, std::string_view suffix)
{
  return std::string(prefix) + std::to_string(i) + std::string(suffix);
}

//! ossia integers are 32-bit, and a "no limit" value such as the int64 maximum
//! that Linux reports for fs.file-max would wrap around
ossia::value count(std::uint64_t v) noexcept
{
  return int(std::min<std::uint64_t>(v, std::numeric_limits<int>::max()));
}

float usage_of(std::uint64_t total, std::uint64_t free) noexcept
{
  if(total == 0)
    return 0.f;
  return float(double(total - std::min(total, free)) / double(total));
}
}

qt_info qt_info::scan()
{
  qt_info info;
  info.version = qVersion();
  info.product = QSysInfo::prettyProductName().toStdString();
  info.architecture = QSysInfo::currentCpuArchitecture().toStdString();
  info.build_abi = QSysInfo::buildAbi().toStdString();

  if(auto* app = qGuiApp)
  {
    info.platform = app->platformName().toStdString();

    if(auto* widgets = qobject_cast<QApplication*>(app))
      if(auto* style = widgets->style())
        info.style = style->objectName().toStdString();

    const auto* primary = app->primaryScreen();
    for(const auto* screen : app->screens())
    {
      const auto geom = screen->geometry();
      const auto avail = screen->availableGeometry();
      const auto phys = screen->physicalSize();

      info.displays.push_back(display{
          .name = screen->name().toStdString(),
          .manufacturer = screen->manufacturer().toStdString(),
          .model = screen->model().toStdString(),
          .serial = screen->serialNumber().toStdString(),
          .x = geom.x(),
          .y = geom.y(),
          .width = geom.width(),
          .height = geom.height(),
          .available_width = avail.width(),
          .available_height = avail.height(),
          .refresh_rate = float(screen->refreshRate()),
          .logical_dpi = float(screen->logicalDotsPerInch()),
          .physical_dpi = float(screen->physicalDotsPerInch()),
          .scale = float(screen->devicePixelRatio()),
          .depth = screen->depth(),
          .physical_width_mm = float(phys.width()),
          .physical_height_mm = float(phys.height()),
          .orientation = to_string(screen->orientation()),
          .primary = screen == primary});
    }
  }

  return info;
}

hardware hardware::scan()
{
  hardware hw;
  hw.cpus = hwinfo::getAllCPUs();
  hw.gpus = hwinfo::getAllGPUs();
  hw.disks = hwinfo::getAllDisks();
  hw.batteries = hwinfo::getAllBatteries();
  hw.networks = hwinfo::getAllNetworks();
  hw.qt = qt_info::scan();

  const auto mount_table = platform::mount_table();

  std::unordered_set<std::string> seen;
  for(std::size_t i = 0; i < hw.disks.size(); i++)
  {
    for(const auto& mp : hw.disks[i].mount_points())
    {
      std::string path = mp;
      if(!mount_table.empty())
      {
        const auto it = mount_table.find(mp);
        if(it == mount_table.end())
          continue;
        path = it->second;
      }

      if(seen.insert(path).second)
        hw.mounts.push_back({int(i), path});
    }
  }

  std::vector<std::uint32_t> gpu_ids;
  gpu_ids.reserve(hw.gpus.size());
  for(const auto& gpu : hw.gpus)
    gpu_ids.push_back(gpu.id());
  platform::init_gpu_readings(gpu_ids);
  hw.gpu_info = platform::gpu_descriptions();

  return hw;
}

snapshot sampler::fetch(const hardware& hw, std::chrono::milliseconds cpu_window)
{
  snapshot s;

  const auto cpu = hwinfo::monitoring::cpu::fetch(cpu_window);
  s.cpu_usage = float(cpu.utilization);
  s.thread_usage.assign(cpu.thread_utilization.begin(), cpu.thread_utilization.end());
  s.thread_frequency.assign(
      cpu.thread_frequency_hz.begin(), cpu.thread_frequency_hz.end());

  const auto ram = hwinfo::monitoring::ram::fetch();
  s.memory_free = ram.free_bytes;
  s.memory_available = ram.available_bytes;

  s.mounts.reserve(hw.mounts.size());
  for(const auto& m : hw.mounts)
  {
    const auto stats = platform::mount_stats(m.path);
    s.mounts.push_back(
        snapshot::mount_state{.total = stats.total, .free = stats.free});
  }

  s.batteries.reserve(hw.batteries.size());
  for(const auto& b : hw.batteries)
  {
    const auto state = b.state();
    s.batteries.push_back(snapshot::battery_state{
        .level = float(b.capacity()),
        .energy_now = float(b.energyNow()),
        .charging = state == hwinfo::Battery::State::CHARGING,
        .state = to_string(state)});
  }

  {
    const auto now = std::chrono::steady_clock::now();
    const auto counters = platform::network_traffic();

    const bool has_previous = m_prev_time.time_since_epoch().count() != 0
                              && m_prev_rx.size() == hw.networks.size();
    const double dt
        = has_previous
              ? std::chrono::duration<double>(now - m_prev_time).count()
              : 0.;

    m_prev_rx.resize(hw.networks.size());
    m_prev_tx.resize(hw.networks.size());
    s.networks.resize(hw.networks.size());

    for(std::size_t i = 0; i < hw.networks.size(); i++)
    {
      // hwinfo names an interface on Linux and macOS, and numbers it on Windows
      auto it = counters.find(hw.networks[i].description());
      if(it == counters.end())
        it = counters.find(hw.networks[i].interfaceIndex());
      if(it == counters.end())
        continue;

      auto& n = s.networks[i];
      n.rx_total = it->second.rx;
      n.tx_total = it->second.tx;

      if(dt > 0.)
      {
        // Counters wrap around, and an interface going down resets them
        if(n.rx_total >= m_prev_rx[i])
          n.rx = float(double(n.rx_total - m_prev_rx[i]) / dt);
        if(n.tx_total >= m_prev_tx[i])
          n.tx = float(double(n.tx_total - m_prev_tx[i]) / dt);
      }

      m_prev_rx[i] = n.rx_total;
      m_prev_tx[i] = n.tx_total;
    }

    m_prev_time = now;
  }

  {
    const auto readings = platform::gpu_readings();
    s.gpus.resize(hw.gpus.size());
    for(std::size_t i = 0; i < hw.gpus.size(); i++)
      if(const auto it = readings.find(hw.gpus[i].id()); it != readings.end())
        s.gpus[i] = it->second;
  }

  s.load = platform::loadavg();
  s.files = platform::open_files();
  s.uptime = platform::uptime();

  return s;
}

void tree::setup(
    ossia::net::node_base& root, const hardware& hw, const SpecificSettings& set)
{
  {
    auto& n = ossia::net::create_node(root, "/rate");
    m_rate = n.create_parameter(ossia::val_type::INT);
    if(m_rate)
    {
      m_rate->set_access(ossia::access_mode::BI);
      m_rate->set_domain(ossia::make_domain(0, SpecificSettings::max_rate));
      ossia::net::set_description(
          n, "Milliseconds between refreshes; 0 stops refreshing altogether");
      // The protocol attaches its callback after this, so this does not
      // re-enter it
      m_rate->push_value(set.rate);
    }
  }

  m_hostname
      = addr(root, "/hostname", ossia::val_type::STRING, "Network name of the machine");
  m_uptime
      = addr(root, "/uptime", ossia::val_type::FLOAT, "Seconds elapsed since boot");

  m_time.iso = addr(
      root, "/time/iso", ossia::val_type::STRING, "Local date and time, ISO 8601");
  m_time.date = addr(root, "/time/date", ossia::val_type::STRING, "Local date");
  m_time.clock = addr(root, "/time/clock", ossia::val_type::STRING, "Local time of day");
  m_time.year = addr(root, "/time/year", ossia::val_type::INT, "Year");
  m_time.month = addr(root, "/time/month", ossia::val_type::INT, "Month, 1 to 12");
  m_time.day = addr(root, "/time/day", ossia::val_type::INT, "Day of the month");
  m_time.weekday = addr(
      root, "/time/weekday", ossia::val_type::INT, "Day of the week, 1 is Monday");
  m_time.hour = addr(root, "/time/hour", ossia::val_type::INT, "Hour, 0 to 23");
  m_time.minute = addr(root, "/time/minute", ossia::val_type::INT, "Minute");
  m_time.second = addr(root, "/time/second", ossia::val_type::INT, "Second");
  m_time.day_seconds = addr(
      root, "/time/day_seconds", ossia::val_type::FLOAT,
      "Seconds elapsed since local midnight");
  m_time.unix_time = addr(
      root, "/time/unix", ossia::val_type::INT, "Seconds since the Unix epoch");
  m_time.timezone
      = addr(root, "/time/timezone", ossia::val_type::STRING, "Local time zone");
  m_time.utc_offset = addr(
      root, "/time/utc_offset", ossia::val_type::INT,
      "Offset from UTC, in minutes");

  m_load_1 = addr(
      root, "/load/1m", ossia::val_type::FLOAT,
      "System load averaged over one minute, 0 where the platform has no equivalent");
  m_load_5 = addr(
      root, "/load/5m", ossia::val_type::FLOAT,
      "System load averaged over five minutes");
  m_load_15 = addr(
      root, "/load/15m", ossia::val_type::FLOAT,
      "System load averaged over fifteen minutes");

  m_files_open = addr(
      root, "/files/open", ossia::val_type::INT,
      "Open file handles machine-wide, 0 on Windows which does not report it");
  m_files_max = addr(
      root, "/files/max", ossia::val_type::INT, "Machine-wide limit on open files");
  m_process_files_open = addr(
      root, "/process/files/open", ossia::val_type::INT,
      "File descriptors held by score; on Windows, every kernel handle it holds");
  m_process_files_max = addr(
      root, "/process/files/max", ossia::val_type::INT,
      "Soft limit for this process, 0 where the platform imposes none");
  m_process_files_usage
      = ratio(root, "/process/files/usage", "Descriptors used against the limit, in [0; 1]");

  m_os_name = addr(root, "/os/name", ossia::val_type::STRING, "Operating system name");
  m_os_product = addr(
      root, "/os/product", ossia::val_type::STRING, "Product name, as Qt reports it");
  m_os_version
      = addr(root, "/os/version", ossia::val_type::STRING, "Operating system version");
  m_os_kernel = addr(root, "/os/kernel", ossia::val_type::STRING, "Kernel version");
  m_os_architecture = addr(
      root, "/os/architecture", ossia::val_type::STRING, "CPU architecture");
  m_os_bits = addr(
      root, "/os/bits", ossia::val_type::INT, "Address width: 32, 64, or 0 if unknown");
  m_os_endianness
      = addr(root, "/os/endianness", ossia::val_type::STRING, "\"little\" or \"big\"");

  m_qt_version = addr(root, "/qt/version", ossia::val_type::STRING, "Qt version");
  m_qt_platform = addr(
      root, "/qt/platform", ossia::val_type::STRING,
      "Platform plugin in use: xcb, wayland, cocoa, windows, offscreen...");
  m_qt_style = addr(root, "/qt/style", ossia::val_type::STRING, "Widget style in use");
  m_qt_abi = addr(root, "/qt/abi", ossia::val_type::STRING, "ABI score was built for");

  m_mainboard_vendor
      = addr(root, "/mainboard/vendor", ossia::val_type::STRING, "Mainboard vendor");
  m_mainboard_name
      = addr(root, "/mainboard/name", ossia::val_type::STRING, "Mainboard model");
  m_mainboard_version
      = addr(root, "/mainboard/version", ossia::val_type::STRING, "Mainboard revision");
  m_mainboard_serial = addr(
      root, "/mainboard/serial", ossia::val_type::STRING, "Mainboard serial number");

  m_cpu_count
      = addr(root, "/cpu/count", ossia::val_type::INT, "Number of physical CPU packages");
  m_cpu_threads = addr(
      root, "/cpu/threads", ossia::val_type::INT, "Total number of logical threads");
  m_cpu_usage = ratio(root, "/cpu/usage", "Average load across all threads, in [0; 1]");

  if(set.perThreadCpu)
  {
    m_cpu_thread_usage = addr(
        root, "/cpu/thread/usage", ossia::val_type::LIST,
        "Per-thread load, one float in [0; 1] per logical thread");
    m_cpu_thread_frequency = addr(
        root, "/cpu/thread/frequency", ossia::val_type::LIST,
        "Per-thread current clock rate, in Hz");
  }

  m_cpus.resize(hw.cpus.size());
  for(std::size_t i = 0; i < m_cpus.size(); i++)
  {
    auto& p = m_cpus[i];
    p.vendor
        = addr(root, idx("/cpu/", i, "/vendor"), ossia::val_type::STRING, "CPU vendor");
    p.model = addr(root, idx("/cpu/", i, "/model"), ossia::val_type::STRING, "CPU model");
    p.physical_cores = addr(
        root, idx("/cpu/", i, "/cores/physical"), ossia::val_type::INT,
        "Number of physical cores");
    p.logical_cores = addr(
        root, idx("/cpu/", i, "/cores/logical"), ossia::val_type::INT,
        "Number of logical threads");
    p.base_frequency = addr(
        root, idx("/cpu/", i, "/frequency/base"), ossia::val_type::FLOAT,
        "Nominal clock rate, in Hz");
    p.max_frequency = addr(
        root, idx("/cpu/", i, "/frequency/max"), ossia::val_type::FLOAT,
        "Maximum clock rate, in Hz");
    p.l1d = addr(
        root, idx("/cpu/", i, "/cache/l1d"), ossia::val_type::FLOAT,
        "L1 data cache size, in bytes");
    p.l1i = addr(
        root, idx("/cpu/", i, "/cache/l1i"), ossia::val_type::FLOAT,
        "L1 instruction cache size, in bytes");
    p.l2 = addr(
        root, idx("/cpu/", i, "/cache/l2"), ossia::val_type::FLOAT,
        "L2 cache size, in bytes");
    p.l3 = addr(
        root, idx("/cpu/", i, "/cache/l3"), ossia::val_type::FLOAT,
        "L3 cache size, in bytes");
    p.flags = addr(
        root, idx("/cpu/", i, "/flags"), ossia::val_type::LIST,
        "Instruction set extensions reported by the CPU");
  }

  m_gpu_count = addr(root, "/gpu/count", ossia::val_type::INT, "Number of GPUs");
  m_gpus.resize(hw.gpus.size());
  for(std::size_t i = 0; i < m_gpus.size(); i++)
  {
    auto& p = m_gpus[i];
    p.vendor
        = addr(root, idx("/gpu/", i, "/vendor"), ossia::val_type::STRING, "GPU vendor");
    p.name = addr(root, idx("/gpu/", i, "/name"), ossia::val_type::STRING, "GPU model");
    p.driver
        = addr(root, idx("/gpu/", i, "/driver"), ossia::val_type::STRING, "Driver version");
    p.memory_total = addr(
        root, idx("/gpu/", i, "/memory/total"), ossia::val_type::FLOAT,
        "Video memory, in bytes");
    p.memory_used = addr(
        root, idx("/gpu/", i, "/memory/used"), ossia::val_type::FLOAT,
        "Video memory in use, in bytes");
    p.memory_shared = addr(
        root, idx("/gpu/", i, "/memory/shared"), ossia::val_type::FLOAT,
        "Memory shared with the host, in bytes");
    p.memory_usage
        = ratio(root, idx("/gpu/", i, "/memory/usage"), "Video memory in use, in [0; 1]");
    p.frequency = addr(
        root, idx("/gpu/", i, "/frequency"), ossia::val_type::FLOAT,
        "GPU clock rate, in Hz");
    p.usage = ratio(root, idx("/gpu/", i, "/usage"), "GPU load, in [0; 1]");
    p.temperature = addr(
        root, idx("/gpu/", i, "/temperature"), ossia::val_type::FLOAT,
        "GPU temperature, in degrees Celsius");
    p.cores
        = addr(root, idx("/gpu/", i, "/cores"), ossia::val_type::INT, "Number of GPU cores");
    p.vendor_id = addr(
        root, idx("/gpu/", i, "/vendor_id"), ossia::val_type::STRING, "PCI vendor id");
    p.device_id = addr(
        root, idx("/gpu/", i, "/device_id"), ossia::val_type::STRING, "PCI device id");
  }

  m_memory_total = addr(
      root, "/memory/total", ossia::val_type::FLOAT, "Total physical memory, in bytes");
  m_memory_free = addr(
      root, "/memory/free", ossia::val_type::FLOAT, "Unused physical memory, in bytes");
  m_memory_available = addr(
      root, "/memory/available", ossia::val_type::FLOAT,
      "Memory available for new allocations, in bytes");
  m_memory_used
      = addr(root, "/memory/used", ossia::val_type::FLOAT, "Memory in use, in bytes");
  m_memory_usage = ratio(root, "/memory/usage", "Memory in use, in [0; 1]");
  m_memory_module_count = addr(
      root, "/memory/module/count", ossia::val_type::INT, "Number of memory modules");

  m_memory_modules.resize(hw.memory.modules().size());
  for(std::size_t i = 0; i < m_memory_modules.size(); i++)
  {
    auto& p = m_memory_modules[i];
    p.vendor = addr(
        root, idx("/memory/module/", i, "/vendor"), ossia::val_type::STRING,
        "Memory module vendor");
    p.name = addr(
        root, idx("/memory/module/", i, "/name"), ossia::val_type::STRING,
        "Memory module name");
    p.model = addr(
        root, idx("/memory/module/", i, "/model"), ossia::val_type::STRING,
        "Memory module model");
    p.serial = addr(
        root, idx("/memory/module/", i, "/serial"), ossia::val_type::STRING,
        "Memory module serial number");
    p.size = addr(
        root, idx("/memory/module/", i, "/size"), ossia::val_type::FLOAT,
        "Memory module size, in bytes");
    p.frequency = addr(
        root, idx("/memory/module/", i, "/frequency"), ossia::val_type::FLOAT,
        "Memory module clock rate, in Hz");
  }

  m_disk_count = addr(root, "/disk/count", ossia::val_type::INT, "Number of disks");
  m_disk_total = addr(
      root, "/disk/total", ossia::val_type::FLOAT,
      "Capacity of every mounted filesystem, in bytes");
  m_disk_free = addr(
      root, "/disk/free", ossia::val_type::FLOAT,
      "Free space across every mounted filesystem, in bytes");
  m_disk_usage = ratio(root, "/disk/usage", "Used disk space, in [0; 1]");

  m_disks.resize(hw.disks.size());
  for(std::size_t i = 0; i < m_disks.size(); i++)
  {
    auto& p = m_disks[i];
    p.vendor
        = addr(root, idx("/disk/", i, "/vendor"), ossia::val_type::STRING, "Disk vendor");
    p.model = addr(root, idx("/disk/", i, "/model"), ossia::val_type::STRING, "Disk model");
    p.serial = addr(
        root, idx("/disk/", i, "/serial"), ossia::val_type::STRING, "Disk serial number");
    p.size = addr(
        root, idx("/disk/", i, "/size"), ossia::val_type::FLOAT,
        "Capacity of the device itself, in bytes");
    p.interface = addr(
        root, idx("/disk/", i, "/interface"), ossia::val_type::STRING,
        "Bus the disk is attached to");
    p.mountpoints = addr(
        root, idx("/disk/", i, "/mountpoints"), ossia::val_type::LIST,
        "Paths the disk is mounted on");
    p.total = addr(
        root, idx("/disk/", i, "/total"), ossia::val_type::FLOAT,
        "Capacity of the mounted filesystems of this disk, in bytes");
    p.free = addr(
        root, idx("/disk/", i, "/free"), ossia::val_type::FLOAT,
        "Free space across the mounted filesystems of this disk, in bytes");
    p.usage = ratio(root, idx("/disk/", i, "/usage"), "Used disk space, in [0; 1]");
  }

  m_battery_count
      = addr(root, "/battery/count", ossia::val_type::INT, "Number of batteries");
  m_battery_level
      = ratio(root, "/battery/level", "Charge of the first battery, in [0; 1]");
  m_battery_charging = addr(
      root, "/battery/charging", ossia::val_type::BOOL,
      "Whether the first battery is charging");

  m_batteries.resize(hw.batteries.size());
  for(std::size_t i = 0; i < m_batteries.size(); i++)
  {
    auto& p = m_batteries[i];
    p.vendor = addr(
        root, idx("/battery/", i, "/vendor"), ossia::val_type::STRING, "Battery vendor");
    p.model = addr(
        root, idx("/battery/", i, "/model"), ossia::val_type::STRING, "Battery model");
    p.serial = addr(
        root, idx("/battery/", i, "/serial"), ossia::val_type::STRING,
        "Battery serial number");
    p.technology = addr(
        root, idx("/battery/", i, "/technology"), ossia::val_type::STRING,
        "Battery chemistry");
    p.level = ratio(root, idx("/battery/", i, "/level"), "Charge, in [0; 1]");
    p.energy_now = addr(
        root, idx("/battery/", i, "/energy/now"), ossia::val_type::FLOAT,
        "Current charge, in the unit reported by the platform");
    p.energy_full = addr(
        root, idx("/battery/", i, "/energy/full"), ossia::val_type::FLOAT,
        "Charge when full, in the unit reported by the platform");
    p.charging
        = addr(root, idx("/battery/", i, "/charging"), ossia::val_type::BOOL, "Charging");
    p.state = addr(
        root, idx("/battery/", i, "/state"), ossia::val_type::STRING,
        "\"charging\", \"discharging\" or \"unknown\"");
  }

  m_network_count = addr(
      root, "/network/count", ossia::val_type::INT, "Number of network interfaces");
  m_network_rx = addr(
      root, "/network/rx", ossia::val_type::FLOAT,
      "Total incoming throughput, in bytes per second");
  m_network_tx = addr(
      root, "/network/tx", ossia::val_type::FLOAT,
      "Total outgoing throughput, in bytes per second");

  m_networks.resize(hw.networks.size());
  for(std::size_t i = 0; i < m_networks.size(); i++)
  {
    auto& p = m_networks[i];
    p.index = addr(
        root, idx("/network/", i, "/index"), ossia::val_type::STRING, "Interface index");
    p.description = addr(
        root, idx("/network/", i, "/description"), ossia::val_type::STRING,
        "Interface name");
    p.mac
        = addr(root, idx("/network/", i, "/mac"), ossia::val_type::STRING, "MAC address");
    p.ipv4
        = addr(root, idx("/network/", i, "/ipv4"), ossia::val_type::STRING, "IPv4 address");
    p.ipv6
        = addr(root, idx("/network/", i, "/ipv6"), ossia::val_type::STRING, "IPv6 address");
    p.rx = addr(
        root, idx("/network/", i, "/rx"), ossia::val_type::FLOAT,
        "Incoming throughput, in bytes per second");
    p.tx = addr(
        root, idx("/network/", i, "/tx"), ossia::val_type::FLOAT,
        "Outgoing throughput, in bytes per second");
    p.rx_total = addr(
        root, idx("/network/", i, "/rx_total"), ossia::val_type::FLOAT,
        "Bytes received since the interface came up");
    p.tx_total = addr(
        root, idx("/network/", i, "/tx_total"), ossia::val_type::FLOAT,
        "Bytes sent since the interface came up");
  }

  m_display_count
      = addr(root, "/display/count", ossia::val_type::INT, "Number of screens");
  m_displays.resize(hw.qt.displays.size());
  for(std::size_t i = 0; i < m_displays.size(); i++)
  {
    auto& p = m_displays[i];
    p.name
        = addr(root, idx("/display/", i, "/name"), ossia::val_type::STRING, "Screen name");
    p.manufacturer = addr(
        root, idx("/display/", i, "/manufacturer"), ossia::val_type::STRING,
        "Screen manufacturer");
    p.model = addr(
        root, idx("/display/", i, "/model"), ossia::val_type::STRING, "Screen model");
    p.serial = addr(
        root, idx("/display/", i, "/serial"), ossia::val_type::STRING,
        "Screen serial number");
    p.width = addr(
        root, idx("/display/", i, "/width"), ossia::val_type::INT, "Width, in pixels");
    p.height = addr(
        root, idx("/display/", i, "/height"), ossia::val_type::INT, "Height, in pixels");
    p.x = addr(
        root, idx("/display/", i, "/x"), ossia::val_type::INT,
        "Position in the virtual desktop, in pixels");
    p.y = addr(
        root, idx("/display/", i, "/y"), ossia::val_type::INT,
        "Position in the virtual desktop, in pixels");
    p.available_width = addr(
        root, idx("/display/", i, "/available/width"), ossia::val_type::INT,
        "Width excluding panels and docks, in pixels");
    p.available_height = addr(
        root, idx("/display/", i, "/available/height"), ossia::val_type::INT,
        "Height excluding panels and docks, in pixels");
    p.refresh_rate = addr(
        root, idx("/display/", i, "/refresh_rate"), ossia::val_type::FLOAT,
        "Refresh rate, in Hz");
    p.logical_dpi = addr(
        root, idx("/display/", i, "/dpi/logical"), ossia::val_type::FLOAT,
        "Logical dots per inch");
    p.physical_dpi = addr(
        root, idx("/display/", i, "/dpi/physical"), ossia::val_type::FLOAT,
        "Physical dots per inch");
    p.scale = addr(
        root, idx("/display/", i, "/scale"), ossia::val_type::FLOAT,
        "Device pixel ratio");
    p.depth = addr(
        root, idx("/display/", i, "/depth"), ossia::val_type::INT,
        "Colour depth, in bits per pixel");
    p.physical_width = addr(
        root, idx("/display/", i, "/physical/width"), ossia::val_type::FLOAT,
        "Physical width, in millimetres");
    p.physical_height = addr(
        root, idx("/display/", i, "/physical/height"), ossia::val_type::FLOAT,
        "Physical height, in millimetres");
    p.orientation = addr(
        root, idx("/display/", i, "/orientation"), ossia::val_type::STRING,
        "Screen orientation");
    p.primary = addr(
        root, idx("/display/", i, "/primary"), ossia::val_type::BOOL,
        "Whether this is the primary screen");
  }
}

void tree::push_static(const hardware& hw)
{
  push(m_hostname, QSysInfo::machineHostName().toStdString());

  push(m_os_name, hw.os.name());
  push(m_os_product, hw.qt.product);
  push(m_os_version, hw.os.version());
  push(m_os_kernel, hw.os.kernel());
  push(m_os_architecture, hw.qt.architecture);
  push(m_os_bits, hw.os.is64bit() ? 64 : (hw.os.is32bit() ? 32 : 0));
  push(m_os_endianness, std::string(hw.os.isBigEndian() ? "big" : "little"));

  push(m_qt_version, hw.qt.version);
  push(m_qt_platform, hw.qt.platform);
  push(m_qt_style, hw.qt.style);
  push(m_qt_abi, hw.qt.build_abi);

  push(m_mainboard_vendor, hw.mainboard.vendor());
  push(m_mainboard_name, hw.mainboard.name());
  push(m_mainboard_version, hw.mainboard.version());
  push(m_mainboard_serial, hw.mainboard.serialNumber());

  push(m_cpu_count, int(hw.cpus.size()));
  {
    int threads = 0;
    for(const auto& cpu : hw.cpus)
      threads += int(cpu.numLogicalCores());
    push(m_cpu_threads, threads);
  }

  for(std::size_t i = 0; i < m_cpus.size(); i++)
  {
    const auto& p = m_cpus[i];
    const auto& cpu = hw.cpus[i];
    push(p.vendor, cpu.vendor());
    push(p.model, cpu.modelName());
    push(p.physical_cores, int(cpu.numPhysicalCores()));
    push(p.logical_cores, int(cpu.numLogicalCores()));
    push(p.flags, strings(cpu.flags()));

    const auto& cores = cpu.cores();
    if(!cores.empty())
    {
      const auto& c = cores.front();
      push(p.base_frequency, frequency(c.regular_frequency_hz));
      push(p.max_frequency, frequency(c.max_frequency_hz));
      push(p.l1d, quantity(c.cache.l1_data));
      push(p.l1i, quantity(c.cache.l1_instruction));
      push(p.l2, quantity(c.cache.l2));
      push(p.l3, quantity(c.cache.l3));
    }
  }

  push(m_gpu_count, int(hw.gpus.size()));
  for(std::size_t i = 0; i < m_gpus.size(); i++)
  {
    const auto& p = m_gpus[i];
    const auto& gpu = hw.gpus[i];
    push(p.vendor, gpu.vendor());
    push(p.name, gpu.name());
    const auto it = hw.gpu_info.find(gpu.id());
    const auto* extra = it != hw.gpu_info.end() ? &it->second : nullptr;

    push(p.driver, extra && !extra->driver.empty() ? extra->driver : gpu.driverVersion());
    push(p.memory_shared, quantity(gpu.shared_memory_Bytes()));
    push(p.cores, int(extra && extra->cores > 0 ? extra->cores : gpu.num_cores()));
    push(p.vendor_id, gpu.vendor_id());
    push(p.device_id, gpu.device_id());
  }

  push(m_memory_total, quantity(hw.memory.size()));

  const auto& modules = hw.memory.modules();
  push(m_memory_module_count, int(modules.size()));
  for(std::size_t i = 0; i < m_memory_modules.size(); i++)
  {
    const auto& p = m_memory_modules[i];
    const auto& m = modules[i];
    push(p.vendor, m.vendor);
    push(p.name, m.name);
    push(p.model, m.model);
    push(p.serial, m.serial_number);
    push(p.size, quantity(m._size_bytes));
    push(p.frequency, quantity(m.frequency_hz));
  }

  push(m_disk_count, int(hw.disks.size()));
  for(std::size_t i = 0; i < m_disks.size(); i++)
  {
    const auto& p = m_disks[i];
    const auto& d = hw.disks[i];
    push(p.vendor, d.vendor());
    push(p.model, d.model());
    push(p.serial, d.serial_number());
    push(p.size, quantity(d.size()));
    push(p.interface, std::string(to_string(d.disk_interface())));

    std::vector<std::string> paths;
    for(const auto& m : hw.mounts)
      if(m.disk == int(i))
        paths.push_back(m.path);
    push(p.mountpoints, strings(paths));
  }

  push(m_battery_count, int(hw.batteries.size()));
  for(std::size_t i = 0; i < m_batteries.size(); i++)
  {
    const auto& p = m_batteries[i];
    const auto& b = hw.batteries[i];
    push(p.vendor, b.vendor());
    push(p.model, b.model());
    push(p.serial, b.serialNumber());
    push(p.technology, b.technology());
    push(p.energy_full, quantity(b.energyFull()));
  }

  push(m_network_count, int(hw.networks.size()));
  for(std::size_t i = 0; i < m_networks.size(); i++)
  {
    const auto& p = m_networks[i];
    const auto& n = hw.networks[i];
    push(p.index, n.interfaceIndex());
    push(p.description, n.description());
    push(p.mac, n.mac());
    push(p.ipv4, n.ip4());
    push(p.ipv6, n.ip6());
  }

  push(m_display_count, int(hw.qt.displays.size()));
  for(std::size_t i = 0; i < m_displays.size(); i++)
  {
    const auto& p = m_displays[i];
    const auto& d = hw.qt.displays[i];
    push(p.name, d.name);
    push(p.manufacturer, d.manufacturer);
    push(p.model, d.model);
    push(p.serial, d.serial);
    push(p.width, d.width);
    push(p.height, d.height);
    push(p.x, d.x);
    push(p.y, d.y);
    push(p.available_width, d.available_width);
    push(p.available_height, d.available_height);
    push(p.refresh_rate, d.refresh_rate);
    push(p.logical_dpi, d.logical_dpi);
    push(p.physical_dpi, d.physical_dpi);
    push(p.scale, d.scale);
    push(p.depth, d.depth);
    push(p.physical_width, d.physical_width_mm);
    push(p.physical_height, d.physical_height_mm);
    push(p.orientation, d.orientation);
    push(p.primary, d.primary);
  }
}

void tree::push_time()
{
  const auto now = QDateTime::currentDateTime();
  const auto date = now.date();
  const auto time = now.time();

  push(m_time.iso, now.toString(Qt::ISODate).toStdString());
  push(m_time.date, date.toString(Qt::ISODate).toStdString());
  push(m_time.clock, time.toString("HH:mm:ss").toStdString());
  push(m_time.year, date.year());
  push(m_time.month, date.month());
  push(m_time.day, date.day());
  push(m_time.weekday, date.dayOfWeek());
  push(m_time.hour, time.hour());
  push(m_time.minute, time.minute());
  push(m_time.second, time.second());
  push(m_time.day_seconds, float(time.msecsSinceStartOfDay()) / 1000.f);
  push(m_time.unix_time, int(now.toSecsSinceEpoch()));
  push(m_time.timezone, now.timeZone().id().toStdString());
  push(m_time.utc_offset, now.offsetFromUtc() / 60);
}

void tree::push_dynamic(const hardware& hw, const snapshot& snap)
{
  push(m_uptime, float(snap.uptime));
  push(m_load_1, snap.load.one);
  push(m_load_5, snap.load.five);
  push(m_load_15, snap.load.fifteen);

  push(m_files_open, count(snap.files.system_open));
  push(m_files_max, count(snap.files.system_max));
  push(m_process_files_open, count(snap.files.process_open));
  push(m_process_files_max, count(snap.files.process_max));
  push(
      m_process_files_usage,
      usage_of(snap.files.process_max, snap.files.process_max - std::min(
                                           snap.files.process_max,
                                           snap.files.process_open)));

  push(m_cpu_usage, snap.cpu_usage);
  push(m_cpu_thread_usage, floats(snap.thread_usage));
  push(m_cpu_thread_frequency, floats(snap.thread_frequency));

  const auto total_memory = hw.memory.size();
  push(m_memory_free, quantity(snap.memory_free));
  push(m_memory_available, quantity(snap.memory_available));
  push(
      m_memory_used,
      quantity(total_memory - std::min(total_memory, snap.memory_available)));
  push(m_memory_usage, usage_of(total_memory, snap.memory_available));

  for(std::size_t i = 0; i < m_gpus.size() && i < snap.gpus.size(); i++)
  {
    const auto& p = m_gpus[i];
    const auto& g = snap.gpus[i];

    // hwinfo only reports a GPU clock on Windows; the platform layer covers
    // Linux, and neither reports anything on macOS.
    const auto total = g.memory_total > 0 ? g.memory_total
                                          : hw.gpus[i].dedicated_memory_Bytes();
    const auto clock = g.clock_hz > 0 ? g.clock_hz : hw.gpus[i].frequency_hz();

    push(p.memory_total, quantity(total));
    push(p.memory_used, quantity(g.memory_used));
    push(p.memory_usage, usage_of(total, total - std::min(total, g.memory_used)));
    push(p.frequency, quantity(clock));
    push(p.usage, g.utilization >= 0.f ? g.utilization : 0.f);
    push(p.temperature, g.temperature >= 0.f ? g.temperature : 0.f);
  }

  // Aggregate the mounted filesystems, per disk and overall
  std::vector<snapshot::mount_state> per_disk(hw.disks.size());
  snapshot::mount_state overall;
  for(std::size_t i = 0; i < hw.mounts.size() && i < snap.mounts.size(); i++)
  {
    const auto disk = hw.mounts[i].disk;
    if(disk >= 0 && std::size_t(disk) < per_disk.size())
    {
      per_disk[disk].total += snap.mounts[i].total;
      per_disk[disk].free += snap.mounts[i].free;
    }
    overall.total += snap.mounts[i].total;
    overall.free += snap.mounts[i].free;
  }

  push(m_disk_total, quantity(overall.total));
  push(m_disk_free, quantity(overall.free));
  push(m_disk_usage, usage_of(overall.total, overall.free));

  for(std::size_t i = 0; i < m_disks.size() && i < per_disk.size(); i++)
  {
    const auto& p = m_disks[i];
    const auto& stats = per_disk[i];
    push(p.total, quantity(stats.total));
    push(p.free, quantity(stats.free));
    push(p.usage, usage_of(stats.total, stats.free));
  }

  for(std::size_t i = 0; i < m_batteries.size() && i < snap.batteries.size(); i++)
  {
    const auto& p = m_batteries[i];
    const auto& b = snap.batteries[i];
    push(p.level, b.level);
    push(p.energy_now, b.energy_now);
    push(p.charging, b.charging);
    push(p.state, b.state);
  }

  if(!snap.batteries.empty())
  {
    push(m_battery_level, snap.batteries.front().level);
    push(m_battery_charging, snap.batteries.front().charging);
  }
  else
  {
    push(m_battery_level, 0.f);
    push(m_battery_charging, false);
  }

  float total_rx = 0.f, total_tx = 0.f;
  for(std::size_t i = 0; i < m_networks.size() && i < snap.networks.size(); i++)
  {
    const auto& p = m_networks[i];
    const auto& n = snap.networks[i];
    push(p.rx, n.rx);
    push(p.tx, n.tx);
    push(p.rx_total, quantity(n.rx_total));
    push(p.tx_total, quantity(n.tx_total));
    total_rx += n.rx;
    total_tx += n.tx;
  }
  push(m_network_rx, total_rx);
  push(m_network_tx, total_tx);
}
}
