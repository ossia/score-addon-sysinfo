#pragma once
#include <verdigris>

namespace SysInfo
{
struct SpecificSettings
{
  //! Milliseconds between refreshes; 0 turns refreshing off
  int rate{1000};
  bool perThreadCpu{true};

  static constexpr int max_rate = 3600000;
};
}

Q_DECLARE_METATYPE(SysInfo::SpecificSettings)
W_REGISTER_ARGTYPE(SysInfo::SpecificSettings)
