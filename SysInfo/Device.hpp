#pragma once
#include <Device/Protocol/DeviceInterface.hpp>

namespace SysInfo
{
class DeviceImplementation final : public Device::OwningDeviceInterface
{
  W_OBJECT(DeviceImplementation)
public:
  DeviceImplementation(
      const Device::DeviceSettings& settings,
      const ossia::net::network_context_ptr& ctx);
  ~DeviceImplementation();

  bool reconnect() override;
  void disconnect() override;

private:
  const ossia::net::network_context_ptr& m_ctx;
};
}
