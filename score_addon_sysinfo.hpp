#pragma once
#include <score/application/ApplicationContext.hpp>
#include <score/plugins/InterfaceList.hpp>
#include <score/plugins/qt_interfaces/FactoryInterface_QtInterface.hpp>
#include <score/plugins/qt_interfaces/PluginRequirements_QtInterface.hpp>

#include <vector>

class score_addon_sysinfo final
    : public score::Plugin_QtInterface
    , public score::FactoryInterface_QtInterface
{
  SCORE_PLUGIN_METADATA(1, "39ac8828-42b0-4a06-98e9-1f4377bbaa96")

public:
  score_addon_sysinfo();
  ~score_addon_sysinfo() override;

private:
  std::vector<score::InterfaceBase*> factories(
      const score::ApplicationContext& ctx,
      const score::InterfaceKey& key) const override;
};
