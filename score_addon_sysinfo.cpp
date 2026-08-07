#include "score_addon_sysinfo.hpp"

#include <score/plugins/FactorySetup.hpp>

#include <SysInfo/ProtocolFactory.hpp>

score_addon_sysinfo::score_addon_sysinfo() { }

score_addon_sysinfo::~score_addon_sysinfo() { }

std::vector<score::InterfaceBase*> score_addon_sysinfo::factories(
    const score::ApplicationContext& ctx, const score::InterfaceKey& key) const
{
  return instantiate_factories<
      score::ApplicationContext, FW<Device::ProtocolFactory, SysInfo::ProtocolFactory>>(
      ctx, key);
}

#include <score/plugins/PluginInstances.hpp>
SCORE_EXPORT_PLUGIN(score_addon_sysinfo)
