#pragma once
#include <cstdint>
namespace ContinuoCorePerf {
enum class Part : unsigned { Input, Options, FrameStep, GpuLoop, CpuRun, AudioFrame,
    AudioMix, ShaderBuild, PipelineBuild, WaitForShaders, Count };
inline constexpr unsigned Count=static_cast<unsigned>(Part::Count);
inline constexpr unsigned WordCount=Count*3;
}
#ifdef __LIBRETRO__
#include <array>
#include <atomic>
#include <chrono>
namespace ContinuoCorePerf {
using Clock=std::chrono::steady_clock;
struct Bucket {std::atomic<std::uint64_t> calls{0},ns{0},peak{0};};
inline std::array<Bucket,Count> buckets;
inline std::atomic<bool> active{false};
using ShaderProgressCallback=void (*)(std::uint64_t,std::uint64_t,int);
inline std::atomic<ShaderProgressCallback> shader_progress_callback{nullptr};
inline void ShaderProgress(std::uint64_t done,std::uint64_t total,int progress_active) {
    if(const auto callback=shader_progress_callback.load(std::memory_order_acquire))callback(done,total,progress_active);
}
inline void Add(Part part,std::uint64_t ns) {
    if(!active.load(std::memory_order_relaxed))return;
    auto& b=buckets[static_cast<unsigned>(part)];
    b.calls.fetch_add(1,std::memory_order_relaxed);b.ns.fetch_add(ns,std::memory_order_relaxed);
    auto peak=b.peak.load(std::memory_order_relaxed);
    while(peak<ns&&!b.peak.compare_exchange_weak(peak,ns,std::memory_order_relaxed)){}
}
struct Scope {
    Part part;Clock::time_point start;
    explicit Scope(Part p):part(p),start(active.load(std::memory_order_relaxed)?Clock::now():Clock::time_point{}){}
    ~Scope(){if(start!=Clock::time_point{})Add(part,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()));}
};
inline unsigned Snapshot(std::uint64_t* out,unsigned size) {
    if(!out||size!=WordCount)return 0;
    active=true;
    for(unsigned i=0;i<Count;++i){out[i*3]=buckets[i].calls.exchange(0,std::memory_order_relaxed);out[i*3+1]=buckets[i].ns.exchange(0,std::memory_order_relaxed);out[i*3+2]=buckets[i].peak.exchange(0,std::memory_order_relaxed);}
    return WordCount;
}
}
#define CONTINUO_PERF_JOIN2(a,b) a##b
#define CONTINUO_PERF_JOIN(a,b) CONTINUO_PERF_JOIN2(a,b)
#define CONTINUO_CORE_SCOPE(part) ContinuoCorePerf::Scope CONTINUO_PERF_JOIN(continuo_scope_,__LINE__)(ContinuoCorePerf::Part::part)
#else
#define CONTINUO_CORE_SCOPE(part) do {} while(false)
#endif
