#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>
// Optional, versioned diagnostic ABI. No game-specific policy or addresses.
struct DiscReadTraceEvent
{
    std::uint64_t sequence=0,ticks=0,offset=0,partition=0,fileOffset=0,fileSize=0,requestId=0;
    std::uint32_t length=0,outputAddress=0,pc=0,lr=0,sp=0,result=0,callerCount=0,reserved=0;
    std::uint32_t callers[8]{};
    std::uint8_t prefix[16]{};
    char path[384]{};
};
static_assert(std::is_trivially_copyable_v<DiscReadTraceEvent>);
static_assert(sizeof(DiscReadTraceEvent)==520);
