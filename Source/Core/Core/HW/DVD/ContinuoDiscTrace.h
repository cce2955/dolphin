#pragma once
// Header-only bounded diagnostic store, shared by DVDThread and libretro exports.
// It changes neither ReadRequest/savestate layout nor the data/timing of reads.
#include "ContinuoDiscTraceAbi.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
namespace ContinuoDiscTrace {
struct Pending { DiscReadTraceEvent event;std::uint64_t generation=0; };
struct Store {
    std::atomic<bool> enabled{false};std::mutex mutex;
    std::map<std::pair<std::uint64_t,std::uint64_t>,Pending> pending;
    std::array<DiscReadTraceEvent,2048> events{};
    std::size_t begin=0,count=0;
    std::uint64_t generation=0,sequence=0,dropped=0;
};
inline Store& Data(){static Store data;return data;}
inline bool Enabled(){return Data().enabled.load(std::memory_order_relaxed);}
inline void Control(bool enabled)
{
    auto& d=Data();std::lock_guard lock(d.mutex);
    d.enabled.store(false,std::memory_order_relaxed);++d.generation;d.pending.clear();
    d.begin=d.count=0;d.sequence=d.dropped=0;
    d.enabled.store(enabled,std::memory_order_relaxed);
}
inline void Submit(const DiscReadTraceEvent& event)
{
    if(!Enabled())return;
    auto& d=Data();std::lock_guard lock(d.mutex);
    if(!d.enabled.load(std::memory_order_relaxed))return;
    if(d.pending.size()>=8192){++d.dropped;return;}
    d.pending[{event.requestId,event.ticks}]={event,d.generation};
}
inline std::optional<Pending> Take(std::uint64_t id,std::uint64_t ticks)
{
    if(!Enabled())return {};
    auto& d=Data();std::lock_guard lock(d.mutex);
    auto found=d.pending.find({id,ticks});if(found==d.pending.end())return {};
    auto out=found->second;d.pending.erase(found);return out;
}
inline void Complete(Pending record,const char* path,std::uint64_t fileOffset,std::uint64_t fileSize,
                     bool success,const std::uint8_t* bytes,std::size_t size)
{
    auto& d=Data();std::lock_guard lock(d.mutex);
    if(!d.enabled.load(std::memory_order_relaxed)||record.generation!=d.generation)return;
    auto& e=record.event;e.sequence=++d.sequence;e.result=success?1:0;e.fileOffset=fileOffset;e.fileSize=fileSize;
    if(path){const auto n=std::min(std::strlen(path),sizeof(e.path)-1);std::memcpy(e.path,path,n);e.path[n]=0;}
    if(bytes)std::memcpy(e.prefix,bytes,std::min(size,sizeof(e.prefix)));
    if(d.count==d.events.size()){d.begin=(d.begin+1)%d.events.size();--d.count;++d.dropped;}
    d.events[(d.begin+d.count)%d.events.size()]=e;++d.count;
}
inline std::size_t Drain(DiscReadTraceEvent* out,std::size_t capacity,std::uint64_t* dropped)
{
    auto& d=Data();std::lock_guard lock(d.mutex);if(dropped)*dropped=d.dropped;
    if(!out)return 0;
    const auto count=std::min(capacity,d.count);
    for(std::size_t i=0;i<count;++i)out[i]=d.events[(d.begin+i)%d.events.size()];
    d.begin=(d.begin+count)%d.events.size();d.count-=count;return count;
}
}
