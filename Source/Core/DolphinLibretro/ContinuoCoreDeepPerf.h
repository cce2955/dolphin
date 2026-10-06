#pragma once
#include <cstdint>
namespace ContinuoCoreDeepPerf {
enum class Part : unsigned {GpuDecode,EfbReadback,StagingMap,BboxReadback,QueryWait,BufferUpload,TargetResize,ConfigUpdate,Count};
inline constexpr unsigned Count=static_cast<unsigned>(Part::Count);
inline constexpr unsigned WordCount=Count*3;
}
#ifdef __LIBRETRO__
#include <array>
#include <atomic>
#include <chrono>
namespace ContinuoCoreDeepPerf {
using Clock=std::chrono::steady_clock;
struct Bucket {std::atomic<std::uint64_t> calls{0},ns{0},peak{0};};
inline std::array<Bucket,Count> buckets;
inline std::atomic<bool> enabled{false};
inline void Add(Part part,std::uint64_t elapsed) {
 if(!enabled.load(std::memory_order_relaxed))return;
 auto& bucket=buckets[static_cast<unsigned>(part)];
 bucket.calls.fetch_add(1,std::memory_order_relaxed);bucket.ns.fetch_add(elapsed,std::memory_order_relaxed);
 auto peak=bucket.peak.load(std::memory_order_relaxed);
 while(peak<elapsed&&!bucket.peak.compare_exchange_weak(peak,elapsed,std::memory_order_relaxed)){}
}
struct Scope {
 Part part;Clock::time_point start;
 explicit Scope(Part value):part(value),start(enabled.load(std::memory_order_relaxed)?Clock::now():Clock::time_point{}){}
 ~Scope(){if(start!=Clock::time_point{})Add(part,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()));}
};
inline unsigned Snapshot(std::uint64_t* out,unsigned size) {
 if(!out||size!=WordCount)return 0;
 enabled=true;
 for(unsigned i=0;i<Count;++i){out[i*3]=buckets[i].calls.exchange(0,std::memory_order_relaxed);out[i*3+1]=buckets[i].ns.exchange(0,std::memory_order_relaxed);out[i*3+2]=buckets[i].peak.exchange(0,std::memory_order_relaxed);}
 return WordCount;
}
}
#define CONTINUO_DEEP_JOIN2(a,b) a##b
#define CONTINUO_DEEP_JOIN(a,b) CONTINUO_DEEP_JOIN2(a,b)
#define CONTINUO_DEEP_SCOPE(part) ContinuoCoreDeepPerf::Scope CONTINUO_DEEP_JOIN(continuo_deep_scope_,__LINE__)(ContinuoCoreDeepPerf::Part::part)
#else
#define CONTINUO_DEEP_SCOPE(part) do {} while(false)
#endif
