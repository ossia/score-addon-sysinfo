#pragma once
#include <verdigris>

namespace SysInfo
{
struct SpecificSettings
{
  int rate{1000};
  bool perThreadCpu{true};
};
}

Q_DECLARE_METATYPE(SysInfo::SpecificSettings)
W_REGISTER_ARGTYPE(SysInfo::SpecificSettings)
