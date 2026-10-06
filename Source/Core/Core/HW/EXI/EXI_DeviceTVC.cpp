// Copyright 2026 TVC-Windows Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceTVC.h"

#include <algorithm>
#include <deque>
#include <mutex>

#include "Common/Logging/Log.h"

namespace ExpansionInterface
{
namespace
{
constexpr u32 CMD_INIT = 0x9;
constexpr u32 CMD_RECV = 0xA;
constexpr u32 CMD_SEND = 0xB;
constexpr u32 CMD_CHK_TX = 0xC;
constexpr u32 CMD_CHK_RX = 0xD;
constexpr u32 CMD_RESET = 0xE;

std::mutex s_queue_mutex;
std::deque<u8> s_to_game;
std::deque<u8> s_from_game;
}

CEXITVCRollback::CEXITVCRollback(Core::System& system) : IEXIDevice(system)
{
  TVCBridge::Reset();
}

void CEXITVCRollback::ImmReadWrite(u32& data, u32 size)
{
  if (size != 4)
  {
    WARN_LOG_FMT(EXPANSIONINTERFACE, "TVC bridge received unsupported immediate size {}", size);
    data = 0;
    return;
  }

  switch (data >> 28)
  {
  case CMD_INIT:
    data = TVCBridge::IDENT;
    break;
  case CMD_RECV:
  {
    std::lock_guard lock(s_queue_mutex);
    if (s_to_game.empty())
    {
      data = 0;
      break;
    }
    data = 0x08000000 | (static_cast<u32>(s_to_game.front()) << 16);
    s_to_game.pop_front();
    break;
  }
  case CMD_SEND:
  {
    std::lock_guard lock(s_queue_mutex);
    if (s_from_game.size() >= TVCBridge::MAX_QUEUE_SIZE)
    {
      data = 0;
      break;
    }
    s_from_game.push_back(static_cast<u8>(data >> 20));
    data = 0x04000000;
    break;
  }
  case CMD_CHK_TX:
  {
    std::lock_guard lock(s_queue_mutex);
    data = s_from_game.size() < TVCBridge::MAX_QUEUE_SIZE ? 0x04000000 : 0;
    break;
  }
  case CMD_CHK_RX:
  {
    std::lock_guard lock(s_queue_mutex);
    data = s_to_game.empty() ? 0 : 0x04000000;
    break;
  }
  case CMD_RESET:
    TVCBridge::Reset();
    data = 0x04000000;
    break;
  default:
    WARN_LOG_FMT(EXPANSIONINTERFACE, "Unknown TVC rollback bridge command {:x}", data);
    data = 0;
    break;
  }
}

namespace TVCBridge
{
std::size_t HostWrite(const u8* data, std::size_t size)
{
  if (data == nullptr || size == 0)
    return 0;
  std::lock_guard lock(s_queue_mutex);
  const std::size_t count = std::min(size, MAX_QUEUE_SIZE - s_to_game.size());
  s_to_game.insert(s_to_game.end(), data, data + count);
  return count;
}

std::size_t HostRead(u8* data, std::size_t size)
{
  if (data == nullptr || size == 0)
    return 0;
  std::lock_guard lock(s_queue_mutex);
  const std::size_t count = std::min(size, s_from_game.size());
  for (std::size_t i = 0; i < count; ++i)
  {
    data[i] = s_from_game.front();
    s_from_game.pop_front();
  }
  return count;
}

std::size_t PendingToGame()
{
  std::lock_guard lock(s_queue_mutex);
  return s_to_game.size();
}

std::size_t PendingFromGame()
{
  std::lock_guard lock(s_queue_mutex);
  return s_from_game.size();
}

void Reset()
{
  std::lock_guard lock(s_queue_mutex);
  s_to_game.clear();
  s_from_game.clear();
}
}  // namespace TVCBridge
}  // namespace ExpansionInterface
