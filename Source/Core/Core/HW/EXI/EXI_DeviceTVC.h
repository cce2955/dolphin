// Copyright 2026 TVC-Windows Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "Common/CommonTypes.h"
#include "Core/HW/EXI/EXI_Device.h"

namespace ExpansionInterface
{
class CEXITVCRollback final : public IEXIDevice
{
public:
  explicit CEXITVCRollback(Core::System& system);
  bool IsPresent() const override { return true; }
  void ImmReadWrite(u32& data, u32 size) override;
};

namespace TVCBridge
{
constexpr u32 IDENT = 0x54564301;
constexpr std::size_t MAX_QUEUE_SIZE = 1024 * 1024;

std::size_t HostWrite(const u8* data, std::size_t size);
std::size_t HostRead(u8* data, std::size_t size);
std::size_t PendingToGame();
std::size_t PendingFromGame();
void Reset();
}  // namespace TVCBridge
}  // namespace ExpansionInterface
