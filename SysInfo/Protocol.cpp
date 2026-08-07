#include "Protocol.hpp"

#include <ossia/network/base/device.hpp>
#include <ossia/network/base/parameter.hpp>
#include <ossia/network/context.hpp>
#include <ossia/network/value/value_conversion.hpp>

#include <algorithm>
#include <utility>

namespace SysInfo
{
namespace
{
//! While refreshing is off, the timer only looks for a new rate
constexpr std::chrono::milliseconds paused_interval{250};
}

sysinfo_protocol::sysinfo_protocol(
    ossia::net::network_context_ptr ctx, SpecificSettings set)
    : protocol_base{flags{}}
    , m_context{std::move(ctx)}
    , m_settings{set}
    , m_timer{m_context->context}
{
}

sysinfo_protocol::~sysinfo_protocol()
{
  stop();
}

void sysinfo_protocol::stop()
{
  // Joins the worker thread: no callback can be running afterwards.
  if(m_monitor)
  {
    m_monitor->stop();
    m_monitor.reset();
  }
  m_timer.stop();
}

void sysinfo_protocol::set_device(ossia::net::device_base& dev)
{
  m_hw = hardware::scan();

  m_tree.setup(dev.get_root_node(), m_hw, m_settings);
  m_tree.push_static(m_hw);

  m_requested_rate = std::clamp(m_settings.rate, 0, SpecificSettings::max_rate);

  if(auto* rate = m_tree.rate_parameter())
  {
    // The node outlives the protocol: the device clears its children before
    // releasing us, so this callback cannot fire on a dangling this.
    rate->add_callback([this](const ossia::value& v) {
      m_requested_rate.store(
          std::clamp(ossia::convert<int>(v), 0, SpecificSettings::max_rate),
          std::memory_order_relaxed);
    });
  }

  apply_rate(m_requested_rate.load(std::memory_order_relaxed));
  m_timer.start([this] { tick(); });
}

void sysinfo_protocol::apply_rate(int ms)
{
  m_active_rate = ms;

  if(m_monitor)
  {
    m_monitor->stop();
    m_monitor.reset();
  }

  if(ms <= 0)
  {
    m_timer.set_delay(paused_interval);
    return;
  }

  const auto interval = std::chrono::milliseconds{ms};
  const auto window = std::clamp(
      interval / 2, std::chrono::milliseconds{20}, std::chrono::milliseconds{200});

  m_monitor = std::make_unique<hwinfo::monitoring::Monitor<snapshot>>(
      [this, window] { return m_sampler.fetch(m_hw, window); },
      [this](const snapshot& s) {
    std::lock_guard lock{m_mutex};
    m_snapshot = s;
    m_dirty = true;
      },
      interval);
  m_monitor->start();

  m_timer.set_delay(interval);
}

void sysinfo_protocol::tick()
{
  if(const int requested = m_requested_rate.load(std::memory_order_relaxed);
     requested != m_active_rate)
    apply_rate(requested);

  if(m_active_rate <= 0)
    return;

  // The clock does not depend on the worker, and must not stop ticking if a
  // reading stalls on e.g. an unresponsive network mount.
  m_tree.push_time();

  snapshot snap;
  {
    std::lock_guard lock{m_mutex};
    if(!m_dirty)
      return;
    snap = std::move(m_snapshot);
    m_dirty = false;
  }

  m_tree.push_dynamic(m_hw, snap);
}

bool sysinfo_protocol::pull(ossia::net::parameter_base&)
{
  return false;
}

bool sysinfo_protocol::push(const ossia::net::parameter_base&, const ossia::value&)
{
  return false;
}

bool sysinfo_protocol::push_raw(const ossia::net::full_parameter_data&)
{
  return false;
}

bool sysinfo_protocol::observe(ossia::net::parameter_base&, bool)
{
  return false;
}

bool sysinfo_protocol::update(ossia::net::node_base&)
{
  return false;
}
}
