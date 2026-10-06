#pragma once

#include <cstddef>

#include "Common/CommonTypes.h"

namespace Memory
{
void TVCRollbackDirtyReset();
void TVCRollbackDirtyEnable(bool enabled);
bool TVCRollbackDirtyEnabled();
u8* TVCRollbackDirtyEnabledByte();

void TVCRollbackMarkDirty(u32 address, size_t size);

u8* TVCRollbackDirtyPhysical();
size_t TVCRollbackDirtyPhysicalSize();

u8* TVCRollbackDirtyMEM1();
u8* TVCRollbackDirtyMEM2();

size_t TVCRollbackDirtyMEM1Size();
size_t TVCRollbackDirtyMEM2Size();
}
