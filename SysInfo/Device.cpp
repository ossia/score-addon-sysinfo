#include "Device.hpp"

#include "Protocol.hpp"
#include "SpecificSettings.hpp"

#include <Explorer/DocumentPlugin/DeviceDocumentPlugin.hpp>

#include <score/document/DocumentContext.hpp>

#include <ossia/network/generic/generic_device.hpp>

#include <QDebug>

#include <wobjectimpl.h>

W_OBJECT_IMPL(SysInfo::DeviceImplementation)

namespace SysInfo
{

DeviceImplementation::DeviceImplementation(
    const Device::DeviceSettings& settings, const ossia::net::network_context_ptr& ctx)
    : OwningDeviceInterface{settings}
    , m_ctx{ctx}
{
  m_capas.canRefreshTree = true;
  m_capas.canAddNode = false;
  m_capas.canRemoveNode = false;
  m_capas.canRenameNode = false;
  m_capas.canSetProperties = false;
  m_capas.canSerialize = false;
}

DeviceImplementation::~DeviceImplementation() { }

bool DeviceImplementation::reconnect()
{
  disconnect();

  try
  {
    const auto& set = m_settings.deviceSpecificSettings.value<SpecificSettings>();

    auto protocol = std::make_unique<sysinfo_protocol>(m_ctx, set);
    auto dev = std::make_unique<ossia::net::generic_device>(
        std::move(protocol), settings().name.toStdString());

    m_dev = std::move(dev);
    deviceChanged(nullptr, m_dev.get());
  }
  catch(const std::runtime_error& e)
  {
    qDebug() << "System info error: " << e.what();
  }
  catch(...)
  {
    qDebug() << "System info error";
  }

  return connected();
}

void DeviceImplementation::disconnect()
{
  OwningDeviceInterface::disconnect();
}
}
