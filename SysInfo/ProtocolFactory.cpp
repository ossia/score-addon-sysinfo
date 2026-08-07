#include "ProtocolFactory.hpp"

#include "Device.hpp"
#include "ProtocolSettingsWidget.hpp"
#include "SpecificSettings.hpp"

#include <Explorer/DocumentPlugin/DeviceDocumentPlugin.hpp>

#include <QObject>
#include <QUrl>

namespace SysInfo
{

QString ProtocolFactory::prettyName() const noexcept
{
  return QObject::tr("System info");
}

QString ProtocolFactory::category() const noexcept
{
  return StandardCategories::util;
}

QUrl ProtocolFactory::manual() const noexcept
{
  return QUrl("https://ossia.io/score-docs/devices/sysinfo-device.html");
}

Device::DeviceEnumerators
ProtocolFactory::getEnumerators(const score::DocumentContext& ctx) const
{
  return {};
}

Device::DeviceInterface* ProtocolFactory::makeDevice(
    const Device::DeviceSettings& settings, const Explorer::DeviceDocumentPlugin& plugin,
    const score::DocumentContext& ctx)
{
  return new DeviceImplementation{settings, plugin.networkContext()};
}

const Device::DeviceSettings& ProtocolFactory::defaultSettings() const noexcept
{
  static const Device::DeviceSettings settings = [] {
    Device::DeviceSettings s;
    s.protocol = static_concreteKey();
    s.name = "sysinfo";
    s.deviceSpecificSettings = QVariant::fromValue(SpecificSettings{});
    return s;
  }();

  return settings;
}

Device::ProtocolSettingsWidget* ProtocolFactory::makeSettingsWidget()
{
  return new ProtocolSettingsWidget;
}

QVariant
ProtocolFactory::makeProtocolSpecificSettings(const VisitorVariant& visitor) const
{
  return makeProtocolSpecificSettings_T<SpecificSettings>(visitor);
}

void ProtocolFactory::serializeProtocolSpecificSettings(
    const QVariant& data, const VisitorVariant& visitor) const
{
  serializeProtocolSpecificSettings_T<SpecificSettings>(data, visitor);
}

bool ProtocolFactory::checkCompatibility(
    const Device::DeviceSettings& a, const Device::DeviceSettings& b) const noexcept
{
  return true;
}
}
