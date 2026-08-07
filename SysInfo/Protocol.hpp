#pragma once
#include <SysInfo/SpecificSettings.hpp>
#include <SysInfo/Tree.hpp>

#include <ossia/detail/timer.hpp>
#include <ossia/network/base/protocol.hpp>
#include <ossia/network/context_functions.hpp>

#include <hwinfo/monitoring/monitor.h>

#include <memory>
#include <mutex>

namespace SysInfo
{
/**
 * @brief Exposes the local machine as a read-only ossia device.
 *
 * The readings that block - CPU load is measured over a sampling window,
 * free space on a network mount can stall - happen on a worker thread; the
 * network context only ever copies the latest snapshot and pushes it.
 */
class sysinfo_protocol final : public ossia::net::protocol_base
{
public:
  sysinfo_protocol(ossia::net::network_context_ptr ctx, SpecificSettings set);
  ~sysinfo_protocol() override;

private:
  void set_device(ossia::net::device_base& dev) override;
  bool pull(ossia::net::parameter_base&) override;
  bool push(const ossia::net::parameter_base&, const ossia::value& v) override;
  bool push_raw(const ossia::net::full_parameter_data&) override;
  bool observe(ossia::net::parameter_base&, bool) override;
  bool update(ossia::net::node_base&) override;

  void tick();

  ossia::net::network_context_ptr m_context;
  SpecificSettings m_settings;
  ossia::timer m_timer;

  hardware m_hw;
  tree m_tree;

  //! Worker thread only
  sampler m_sampler;

  std::mutex m_mutex;
  snapshot m_snapshot;
  bool m_dirty{};

  std::unique_ptr<hwinfo::monitoring::Monitor<snapshot>> m_monitor;
};
}
