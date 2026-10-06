#include "Core/HW/TVCRollbackDirty.h"

#include <array>
#include <algorithm>

namespace Memory
{
namespace
{
constexpr size_t PAGE_SHIFT = 12;
constexpr size_t PAGE_SIZE = 1 << PAGE_SHIFT;

constexpr u32 MEM1_BASE = 0x00000000;
constexpr u32 MEM1_SIZE = 24 * 1024 * 1024;

constexpr u32 MEM2_BASE = 0x10000000;
constexpr u32 MEM2_SIZE = 64 * 1024 * 1024;

constexpr size_t MEM1_PAGES = MEM1_SIZE / PAGE_SIZE;
constexpr size_t MEM2_PAGES = MEM2_SIZE / PAGE_SIZE;
constexpr size_t PHYSICAL_PAGES = 0x40000000ull / PAGE_SIZE;
constexpr size_t MEM2_PAGE_BASE = MEM2_BASE / PAGE_SIZE;

std::array<u8, PHYSICAL_PAGES> s_physical_dirty{};

alignas(64) u8 s_enabled = 0;
}

void TVCRollbackDirtyReset()
{
  std::fill(s_physical_dirty.begin(), s_physical_dirty.end(), 0);
}

void TVCRollbackDirtyEnable(bool enabled)
{
  s_enabled = enabled ? 1 : 0;
}

bool TVCRollbackDirtyEnabled()
{
  return s_enabled != 0;
}

u8* TVCRollbackDirtyEnabledByte()
{
  return &s_enabled;
}

void TVCRollbackMarkDirty(u32 address, size_t size)
{
  if (!TVCRollbackDirtyEnabled() || size == 0)
    return;

  address &= 0x3fffffff;

  const size_t first_page = static_cast<size_t>(address) >> PAGE_SHIFT;
  const u64 last_address =
      std::min<u64>(static_cast<u64>(address) + size - 1, 0x3fffffffull);
  const size_t last_page = static_cast<size_t>(last_address) >> PAGE_SHIFT;

  for (size_t page = first_page;
       page <= last_page && page < s_physical_dirty.size(); ++page)
  {
    s_physical_dirty[page] = 1;
  }
}

u8* TVCRollbackDirtyPhysical()
{
  return s_physical_dirty.data();
}

size_t TVCRollbackDirtyPhysicalSize()
{
  return s_physical_dirty.size();
}

u8* TVCRollbackDirtyMEM1()
{
  return s_physical_dirty.data();
}

u8* TVCRollbackDirtyMEM2()
{
  return s_physical_dirty.data() + MEM2_PAGE_BASE;
}

size_t TVCRollbackDirtyMEM1Size()
{
  return MEM1_PAGES;
}

size_t TVCRollbackDirtyMEM2Size()
{
  return MEM2_PAGES;
}
}
