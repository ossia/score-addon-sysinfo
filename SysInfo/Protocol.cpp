#include "Protocol.hpp"

#include <ossia/network/base/device.hpp>
#include <ossia/network/context.hpp>

#include <algorithm>
#include <utility>

namespace SysInfo
{

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
  // Joins the worker thread: no callback can be running afterwards.
  if(m_monitor)
    m_monitor->stop();
  m_timer.stop();
}

void sysinfo_protocol::set_device(ossia::net::device_base& dev)
{
  m_hw = hardware::scan();

  m_tree.setup(dev.get_root_node(), m_hw, m_settings);
  m_tree.push_static(m_hw);

  const auto interval = std::chrono::milliseconds{std::max(50, m_settings.rate)};
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
  m_timer.start([this] { tick(); });
}

void sysinfo_protocol::tick()
{
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
