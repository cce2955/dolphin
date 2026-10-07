#include "Core/HW/DVD/ContinuoDiscTrace.h"
extern "C" RETRO_API bool continuo_disc_trace_control_v1(bool enabled)
{
  ContinuoDiscTrace::Control(enabled);
  return true;
}
extern "C" RETRO_API size_t continuo_disc_trace_read_v1(void* destination,size_t event_size,
                                                        size_t capacity,uint64_t* dropped)
{
  if(event_size!=sizeof(DiscReadTraceEvent)||capacity>4096)return 0;
  return ContinuoDiscTrace::Drain(static_cast<DiscReadTraceEvent*>(destination),capacity,dropped);
}
// Continuo file overrides v1.
#include "Core/HW/DVD/ContinuoDiscOverride.h"
extern "C" RETRO_API bool continuo_disc_override_configure_v1(const char* root)
{
  try {return ContinuoDiscOverride::Configure(root);} catch (...) {return false;}
}

extern "C" RETRO_API bool continuo_disc_override_features_v1(const char* root,unsigned features)
{
  try {return ContinuoDiscOverride::Configure(root,features);} catch (...) {return false;}
}

#include "Core/Core.h"
#include "Core/System.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/DVD/DVDThread.h"
extern "C" RETRO_API bool continuo_disc_override_advance_v1(const char* resource)
{
  try {
    if(!resource||!*resource||std::string(resource).size()>64)return false;
    auto& system=Core::System::GetInstance();
    const auto state=Core::GetState(system);
    if(state!=Core::State::Running && state!=Core::State::Paused)return false;
    const Core::CPUThreadGuard guard(system);
    system.GetDVDThread().WaitForContinuoReads();
    auto& memory=system.GetMemory();
    auto* mem1=memory.GetPointerForRange(0x80000000,24u*1024u*1024u);
    auto* mem2=memory.GetPointerForRange(0x90000000,64u*1024u*1024u);
    if(!mem1||!mem2)return false;
    return ContinuoDiscOverride::Advance(resource,{mem1,24u*1024u*1024u},{mem2,64u*1024u*1024u});
  } catch (...) {return false;}
}

extern "C" RETRO_API bool continuo_disc_override_poll_v1()
{
  try { ContinuoDiscOverride::PollImports(); auto& state=ContinuoDiscOverride::Data(); std::lock_guard lock(state.mutex); return bool(state.prepared); } catch (...) { return false; }
}

// Character playback changes immutable mixer snapshots, never guest stream state.
extern "C" RETRO_API bool continuo_character_music_select_v1(unsigned character,bool randomize)
{
  try { return ContinuoCharacterMusic::Select(character,randomize); } catch (...) {return false;}
}
extern "C" RETRO_API size_t continuo_character_music_status_v1(char* destination,size_t capacity)
{
  if(!destination||capacity<1||capacity>4096)return 0;
  try {const auto text=ContinuoCharacterMusic::Status();const auto count=(std::min)(text.size(),capacity-1);
       std::copy_n(text.data(),count,destination);destination[count]=0;return count;} catch (...) {destination[0]=0;return 0;}
}


extern "C" RETRO_API bool continuo_stage_music_control_v1(bool enabled,bool randomize,bool restart,bool battleActive)
{
  try {ContinuoStageMusic::Control(enabled,randomize,restart,battleActive);return true;}catch(...){return false;}
}
extern "C" RETRO_API size_t continuo_stage_music_status_v1(char* destination,size_t capacity)
{
  if(!destination||capacity<1||capacity>4096)return 0;
  try {const auto text=ContinuoStageMusic::Status();const auto count=(std::min)(text.size(),capacity-1);
       std::copy_n(text.data(),count,destination);destination[count]=0;return count;}
  catch(...){destination[0]=0;return 0;}
}

extern "C" RETRO_API bool continuo_music_output_mode_v1(unsigned mode)
{
  if(mode>2)return false;
  ContinuoMusicMixer::SetOutputMode(mode);
  return true;
}
