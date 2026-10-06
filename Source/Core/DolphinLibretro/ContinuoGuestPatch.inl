// Included once at the end of DolphinLibretro/Main.cpp. Optional versioned ABI.
// No TvC addresses or gameplay policy belong in this core interface.
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/JitInterface.h"

extern "C" RETRO_API bool continuo_patch_words_v1(const uint32_t* words, size_t count,
                                                  unsigned flags)
{
  auto& system = Core::System::GetInstance();
  const auto state = Core::GetState(system);
  if (!words || count == 0 || count > 128 || flags > 1 ||
      (state != Core::State::Running && state != Core::State::Paused))
    return false;
  const Core::CPUThreadGuard guard(system);
  auto& memory = system.GetMemory();
  // Preflight every expected word before changing anything. Only aligned MEM1/MEM2
  // words are accepted, including when the supplied pointer/address wraps.
  for (size_t i = 0; i < count; ++i)
  {
    const u32 address = words[i * 3];
    const bool ram = (address >= 0x80000000 && address <= 0x817ffffc) ||
                     (address >= 0x90000000 && address <= 0x93fffffc);
    if (!ram || (address & 3) || !memory.GetPointerForRange(address, 4) ||
        memory.Read_U32(address) != words[i * 3 + 1])
      return false;
    for (size_t j = 0; j < i; ++j)
      if (words[j * 3] == address)
        return false;
  }
  for (size_t i = 0; i < count; ++i)
  {
    if (words[i * 3 + 1] == words[i * 3 + 2])
      continue;
    const u32 address = words[i * 3];
    memory.Write_U32(words[i * 3 + 2], address);
    if (flags & 1)
      system.GetPPCState().iCache.Invalidate(memory, system.GetJitInterface(), address);
  }
  return true;
}
