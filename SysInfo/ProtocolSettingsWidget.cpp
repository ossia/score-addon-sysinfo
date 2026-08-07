#include "ProtocolSettingsWidget.hpp"

#include "ProtocolFactory.hpp"
#include "SpecificSettings.hpp"

#include <State/Widgets/AddressFragmentLineEdit.hpp>

#include <QCheckBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVariant>

#include <wobjectimpl.h>

W_OBJECT_IMPL(SysInfo::ProtocolSettingsWidget)

namespace SysInfo
{

ProtocolSettingsWidget::ProtocolSettingsWidget(QWidget* parent)
    : Device::ProtocolSettingsWidget(parent)
{
  m_deviceNameEdit = new State::AddressFragmentLineEdit{this};
  checkForChanges(m_deviceNameEdit);
  m_deviceNameEdit->setText("sysinfo");

  m_rate = new QSpinBox{this};
  m_rate->setRange(50, 600000);
  m_rate->setSingleStep(100);
  m_rate->setSuffix(tr(" ms"));
  m_rate->setValue(SpecificSettings{}.rate);

  m_perThreadCpu = new QCheckBox{this};
  m_perThreadCpu->setChecked(SpecificSettings{}.perThreadCpu);
  m_perThreadCpu->setToolTip(
      tr("Expose the load and clock rate of every logical thread as lists "
         "under /cpu/thread"));

  auto layout = new QFormLayout;
  layout->addRow(tr("Name"), m_deviceNameEdit);
  layout->addRow(tr("Refresh rate"), m_rate);
  layout->addRow(tr("Per-thread CPU"), m_perThreadCpu);

  setLayout(layout);
}

ProtocolSettingsWidget::~ProtocolSettingsWidget() { }

Device::DeviceSettings ProtocolSettingsWidget::getSettings() const
{
  Device::DeviceSettings s;
  s.name = m_deviceNameEdit->text();
  s.protocol = ProtocolFactory::static_concreteKey();

  SpecificSettings settings;
  settings.rate = m_rate->value();
  settings.perThreadCpu = m_perThreadCpu->isChecked();
  s.deviceSpecificSettings = QVariant::fromValue(settings);

  return s;
}

void ProtocolSettingsWidget::setSettings(const Device::DeviceSettings& settings)
{
  m_deviceNameEdit->setText(settings.name);

  if(settings.deviceSpecificSettings.canConvert<SpecificSettings>())
  {
    const auto& specific = settings.deviceSpecificSettings.value<SpecificSettings>();
    m_rate->setValue(specific.rate);
    m_perThreadCpu->setChecked(specific.perThreadCpu);
  }
}
}
