#include "SpecificSettings.hpp"

#include <score/serialization/DataStreamVisitor.hpp>
#include <score/serialization/JSONVisitor.hpp>

template <>
void DataStreamReader::read(const SysInfo::SpecificSettings& n)
{
  m_stream << n.rate << n.perThreadCpu;
  insertDelimiter();
}

template <>
void DataStreamWriter::write(SysInfo::SpecificSettings& n)
{
  m_stream >> n.rate >> n.perThreadCpu;
  checkDelimiter();
}

template <>
void JSONReader::read(const SysInfo::SpecificSettings& n)
{
  obj["Rate"] = n.rate;
  obj["PerThreadCpu"] = n.perThreadCpu;
}

template <>
void JSONWriter::write(SysInfo::SpecificSettings& n)
{
  n.rate <<= obj["Rate"];
  n.perThreadCpu <<= obj["PerThreadCpu"];
}
