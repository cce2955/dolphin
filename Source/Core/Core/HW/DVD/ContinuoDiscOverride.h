#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <chrono>
#include <memory>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include "DiscIO/Filesystem.h"
#include "DiscIO/Volume.h"
#include "ContinuoCharacterMusic.h"
#include "ContinuoAssetPaths.h"
namespace ContinuoDiscOverride {
struct Asset {std::string name,group,track;std::vector<std::uint8_t> original;std::vector<std::vector<std::uint8_t>> replacements;std::size_t selected=0;std::filesystem::path rotationPath;std::size_t nextRotation=0;std::size_t entryIndex=0;std::vector<std::string> tracks,labels;std::vector<std::vector<std::uint8_t>> decoderEntries;bool randomize=false;std::size_t selectable=0;};
struct State {
    std::mutex mutex;
    std::shared_ptr<std::vector<Asset>> assets;
    std::shared_ptr<std::vector<Asset>> prepared;
    std::future<std::shared_ptr<std::vector<Asset>>> importJob;
    std::filesystem::path root;
    bool nativeSelectEnabled=false;
    bool allowLegacyOverrides=false;
    std::string generation,loadingGeneration;
    std::atomic<bool> enabled{false},metadataReady{false};
    std::vector<std::uint8_t> metadataCoverage;
    std::size_t metadataPending=0;
    std::filesystem::path statusPath;
    std::vector<std::string> messages;
    std::vector<std::string> rotationCommitted;
};
inline State& Data(){static State state;return state;}
// The DVD worker only serves a fixed selection. Restarting a stream must not
// change its bytes while the game retains the selected decoder/loop state.
inline void Record(State& state,const std::string& message)
{
    if(std::find(state.messages.begin(),state.messages.end(),message)!=state.messages.end()||state.messages.size()>=128)return;
    state.messages.push_back(message);
    std::ofstream out(state.statusPath);
    if(out)for(const auto& line:state.messages)out<<line<<'\n';
}
inline bool Read(const std::filesystem::path& path,std::vector<std::uint8_t>& bytes)
{
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return false;
    const auto size=file.tellg();if(size<=0||size>32*1024*1024)return false;
    bytes.resize(static_cast<std::size_t>(size));file.seekg(0);return bool(file.read(reinterpret_cast<char*>(bytes.data()),size));
}
inline std::uint32_t Be32(const std::vector<std::uint8_t>& bytes,std::size_t at)
{return (std::uint32_t(bytes.at(at))<<24)|(std::uint32_t(bytes.at(at+1))<<16)|(std::uint32_t(bytes.at(at+2))<<8)|bytes.at(at+3);}
inline void PutBe32(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value)
{bytes.at(at)=std::uint8_t(value>>24);bytes.at(at+1)=std::uint8_t(value>>16);bytes.at(at+2)=std::uint8_t(value>>8);bytes.at(at+3)=std::uint8_t(value);}
inline std::size_t SrtEntry(const std::vector<std::uint8_t>& bytes,std::size_t index)
{
    if(bytes.size()<16)throw std::runtime_error("short SRT");
    const auto base=Be32(bytes,12);
    const auto table=std::size_t(base)+16+index*4;
    if(table+4>bytes.size())throw std::runtime_error("SRT index out of range");
    const auto entry=std::size_t(base)+16+Be32(bytes,table);
    if(entry+144>bytes.size())throw std::runtime_error("SRT entry out of range");
    return entry;
}
inline bool SafeRelative(const std::filesystem::path& path)
{
    if(path.empty()||path.is_absolute()||path.has_root_name())return false;
    for(const auto& part:path)if(part=="..")return false;
    return true;
}
inline std::size_t MappedEntry(const std::string& resource)
{
    // Literal resource/DSP mapping from TvCDolphinRiivolution Start.bat.
    // Song choice 15 is Training: stage_17, entry 44. It is not stage_15.
    for(unsigned i=1;i<=14;++i) {
        const auto name=std::string("stage_")+(i<10?"0":"")+std::to_string(i);
        if(resource==name)return 29+i;
    }
    if(resource=="stage_17")return 44;
    for(unsigned i=19;i<=47;++i)
        if(resource=="s_bgm_"+std::to_string(i))return i-18;
    throw std::runtime_error("unknown Riivolution music resource: "+resource);
}
inline void LoadPlaylists(const std::filesystem::path& folder,std::vector<Asset>& assets)
{
    const auto originals=folder/"originals",playlists=folder/"playlists";
    std::vector<std::filesystem::path> stages;
    for(const auto& item:std::filesystem::directory_iterator(playlists))if(item.is_directory()&&item.path().filename().string().front()!='.')stages.push_back(item.path());
    std::sort(stages.begin(),stages.end());
    bool hasTracks=false;
    for(const auto& stage:stages)for(const auto& item:std::filesystem::directory_iterator(stage))if(item.is_directory()&&item.path().filename().string().front()!='.')hasTracks=true;
    if(!hasTracks)return;
    std::vector<std::uint8_t> originalSrt;
    if(!Read(originals/"bgm.srt",originalSrt))throw std::runtime_error("missing originals/bgm.srt");
    Asset metadata;metadata.name="bgm.srt";metadata.original=originalSrt;metadata.replacements.emplace_back();
    auto& merged=metadata.replacements.back();merged=originalSrt;
    std::random_device entropy;std::mt19937 random(entropy());
    std::size_t stageCount=0,totalAssetBytes=originalSrt.size();
    for(const auto& stage:stages)
    {
        const auto stageName=stage.filename().string(),assetName=stageName+".ssd";
        std::vector<std::filesystem::path> songs;
        for(const auto& item:std::filesystem::directory_iterator(stage))if(item.is_directory()&&item.path().filename().string().front()!='.')songs.push_back(item.path());
        std::sort(songs.begin(),songs.end());
        const bool ordered=std::filesystem::exists(stage/"sequence.txt");
        if(ordered) {
            songs.clear();std::ifstream sequence(stage/"sequence.txt");std::string name;
            while(sequence>>name) {
                if(songs.size()>=63||!SafeRelative(name)||std::filesystem::path(name).filename()!=name||
                   !std::filesystem::is_directory(stage/name)||
                   std::find(songs.begin(),songs.end(),stage/name)!=songs.end())
                    throw std::runtime_error("invalid playlist sequence in "+stageName);
                songs.push_back(stage/name);
            }
            if(!sequence.eof()||songs.empty())throw std::runtime_error("empty or unreadable playlist sequence in "+stageName);
        }
        if(songs.empty())continue;
        if(++stageCount>63)throw std::runtime_error("too many stage playlists");
        const auto baseAudio=originals/assetName;
        std::vector<std::uint8_t> originalAudio;
        if(!Read(baseAudio,originalAudio))throw std::runtime_error("missing original stage asset: "+assetName);
        std::ifstream entryFile(stage/"entry.txt");std::size_t entryIndex=0;
        if(!(entryFile>>entryIndex)||entryIndex>=4096)throw std::runtime_error("invalid entry.txt in "+stageName);
        if(entryIndex!=MappedEntry(stageName))throw std::runtime_error("song resource/DSP entry mismatch in "+stageName);
        std::uniform_int_distribution<std::size_t> choose(0,songs.size()-1);
        std::ifstream policyFile(stage/"policy.txt");std::string policy;policyFile>>policy;
        const bool randomize=policy=="random";
        const std::size_t pick=ordered&&!randomize?0:choose(random);
        Asset audio;audio.name=assetName;audio.group=stageName;audio.entryIndex=entryIndex;
        audio.original=std::move(originalAudio);audio.selected=pick;audio.randomize=randomize;
        for(const auto& song:songs) {
            std::vector<std::uint8_t> songSrt,songAudio;
            if(!Read(song/"bgm.srt",songSrt)||!Read(song/assetName,songAudio))throw std::runtime_error("incomplete track folder: "+song.filename().string());
            if(songAudio.size()!=audio.original.size()||songSrt.size()!=originalSrt.size())throw std::runtime_error("playlist assets must match original sizes");
            totalAssetBytes+=songAudio.size()+songSrt.size();
            if(totalAssetBytes>512u*1024u*1024u)throw std::runtime_error("stage playlists exceed the 512 MiB safety limit");
            const auto entry=SrtEntry(songSrt,entryIndex);
            audio.decoderEntries.emplace_back(songSrt.begin()+entry,songSrt.begin()+entry+144);
            std::ifstream titleFile(song/"title.txt");std::string title;std::getline(titleFile,title);
            audio.labels.push_back(title.empty()?song.filename().string():title);
            audio.tracks.push_back(song.filename().string());audio.replacements.push_back(std::move(songAudio));
        }
        audio.selectable=audio.decoderEntries.size();
        audio.track=audio.tracks[pick];
        const auto target=SrtEntry(merged,entryIndex);
        std::copy_n(audio.decoderEntries[pick].begin(),144,merged.begin()+target);
        assets.push_back(std::move(audio));
    }
    if(stageCount==0)return;
    assets.insert(assets.begin(),std::move(metadata));
}
// Native menu assets use their full disc path and do not depend on audio metadata.
inline void LoadNativeSelect(const std::filesystem::path& folder,std::vector<Asset>& assets)
{
    const auto native=ContinuoAssetPaths::NativeSelect(folder);
    if(!std::filesystem::exists(native/"menu.fpk"))return;
    Asset asset;asset.name="fpack/menu/001/0000.fpk";
    std::vector<std::uint8_t> patched;
    if(!Read(native/"original.fpk",asset.original)||!Read(native/"menu.fpk",patched)||
       patched.size()!=asset.original.size())throw std::runtime_error("invalid native select archive pair");
    asset.replacements.push_back(std::move(patched));assets.push_back(std::move(asset));
}
inline auto BuildBattleCodecs(const std::vector<Asset>& assets,const std::filesystem::path& root={})
{
    std::vector<ContinuoCharacterMusic::Codec> codecs;
    const auto meta=std::find_if(assets.begin(),assets.end(),[](const Asset& a){return a.name=="bgm.srt";});
    std::vector<std::uint8_t> original;
    if(meta!=assets.end())original=meta->original;
    else if(!root.empty())Read(root/"originals/bgm.srt",original);
    if(!original.empty()) {
        for(unsigned entry=1;entry<=44;++entry) {
            const auto at=SrtEntry(original,entry);
            ContinuoCharacterMusic::AddEntry(codecs,{original.data()+at,144},entry);
        }
        for(const auto& audio:assets)if(audio.entryIndex)
            for(const auto& entry:audio.decoderEntries)ContinuoCharacterMusic::AddEntry(codecs,entry,static_cast<unsigned>(audio.entryIndex));
    }
    return std::make_shared<const std::vector<ContinuoCharacterMusic::Codec>>(std::move(codecs));
}
inline std::string TrackLabel(const Asset& audio,std::size_t selected)
{return selected<audio.labels.size()?audio.labels[selected]:(selected<audio.tracks.size()?audio.tracks[selected]:audio.track);}
// Configure only while no game is loaded. Immutable assets are loaded once, outside the DVD worker.
inline std::string PlaylistVersion(const std::filesystem::path& root)
{std::ifstream file(root/"playlist-version.txt");std::string value;file>>value;return value;}
inline bool Configure(const char* root,unsigned features=3)
{
    if(features&~7u)return false;
    auto next=std::make_shared<std::vector<Asset>>();
    if(root && *root) {
        const auto folder=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(root)));
        if(features&1u) {
        if(std::filesystem::is_directory(folder/"playlists")&&std::filesystem::exists(folder/"legacy-ssd-mode.txt")) {
            LoadPlaylists(folder,*next);
        } else if(std::filesystem::exists(folder/"legacy-ssd-mode.txt")) {
        std::ifstream manifest(folder/"assets.txt");
        std::string line;
        while(std::getline(manifest,line)) {
            if(line.empty()||line[0]=='#')continue;
            std::istringstream row(line);
            std::vector<std::string> fields;std::string field;
            while(row>>field)fields.push_back(field);
            if(fields.size()<3||next->size()>=32)return false;
            const auto safe=[](const std::string& value) {
                const auto utf8=std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size());
                return SafeRelative(std::filesystem::path(utf8));
            };
            Asset a;a.name=fields[0];
            std::size_t firstCandidate=2;
            if(fields.size()>=4&&fields[2].rfind("group=",0)==0) {
                a.group=fields[2].substr(6);if(a.group.empty())return false;firstCandidate=3;
            }
            if(!safe(a.name)||!safe(fields[1])||firstCandidate>=fields.size())return false;
            if(!Read(folder/fields[1],a.original))return false;
            for(std::size_t i=firstCandidate;i<fields.size();++i) {
                if(!safe(fields[i]))return false;
                std::vector<std::uint8_t> replacement;
                if(!Read(folder/fields[i],replacement)||replacement.size()!=a.original.size())return false;
                a.replacements.push_back(std::move(replacement));
            }
            next->push_back(std::move(a));
        }
        if(manifest.bad())return false;
        std::map<std::string,std::size_t> groupSelections;
        std::random_device entropy;std::mt19937 random(entropy());
        for(auto& asset:*next) {
            if(asset.group.empty())continue;
            const auto found=groupSelections.find(asset.group);
            if(found==groupSelections.end()) {
                std::uniform_int_distribution<std::size_t> choose(0,asset.replacements.size()-1);
                groupSelections.emplace(asset.group,choose(random));
            }
            asset.selected=groupSelections.at(asset.group);
            if(asset.selected>=asset.replacements.size())return false;
        }
        }
        }
        if(features&2u)LoadNativeSelect(folder,*next);
    }
    auto& state=Data();std::lock_guard lock(state.mutex);
    if(state.importJob.valid())state.importJob.wait();
    state.importJob={};state.prepared.reset();
    state.nativeSelectEnabled=(features&2u)!=0;
    state.allowLegacyOverrides=(features&1u)!=0;
    state.root=(features&(1u|4u))&&root&&*root?std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(root))):std::filesystem::path{};
    state.generation=state.root.empty()?std::string{}:PlaylistVersion(state.root);state.loadingGeneration.clear();
    ContinuoCharacterMusic::Configure(state.root);
    ContinuoStageMusic::Configure(state.root);
    ContinuoCharacterMusic::Data().codecs.store(BuildBattleCodecs(*next,state.root));
    state.assets=std::move(next);state.metadataCoverage.clear();state.metadataPending=0;state.messages.clear();state.rotationCommitted.clear();
    state.statusPath=root && *root ? std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(root)))/"playback-status.txt" : std::filesystem::path{};
    for(const auto& asset:*state.assets) {
        if(asset.name=="bgm.srt") {
            state.metadataCoverage.resize(asset.original.size(),1);
            const auto& replacement=asset.replacements[asset.selected];
            for(std::size_t i=0;i<asset.original.size();++i)if(asset.original[i]!=replacement[i]) {
                state.metadataCoverage[i]=0;++state.metadataPending;
            }
        } else Record(state,"Selected "+asset.name+": "+(asset.track.empty()?std::to_string(asset.selected):TrackLabel(asset,asset.selected)));
    }
    state.metadataReady.store(state.metadataPending==0,std::memory_order_release);
    state.enabled.store(state.allowLegacyOverrides && (!state.root.empty()||!state.assets->empty()),std::memory_order_release);
    if(!state.root.empty()&&!std::filesystem::exists(state.root/"legacy-ssd-mode.txt"))Record(state,"Stage PCM configured; native SSD/SRT unchanged. Playback is offline only.");
    if(!state.assets->empty())Record(state,"Configured; selected tracks stay paired through stream restart. Waiting for matching bgm.srt reads.");
    return true;
}
// Polling loads changed files on a worker. Guest RAM is touched only by Advance
// at a guarded loading boundary after outstanding DVD reads are drained.
inline void PollImports()
{
    ContinuoCharacterMusic::Poll();
    ContinuoStageMusic::Poll();
    auto& state=Data();std::lock_guard lock(state.mutex);
    if(state.root.empty()||!state.allowLegacyOverrides)return;
    if(!std::filesystem::exists(state.root/"legacy-ssd-mode.txt"))return;
    if(state.importJob.valid()&&state.importJob.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto result=state.importJob.get();
            if(PlaylistVersion(state.root)==state.loadingGeneration) {
                state.prepared=std::move(result);state.generation=state.loadingGeneration;
                Record(state,"Imported playlists loaded in background; waiting for a match boundary");
            }
        }catch(const std::exception& e) {state.generation=state.loadingGeneration;Record(state,std::string("Playlist import refused: ")+e.what());}
    }
    const auto version=PlaylistVersion(state.root);
    if(!version.empty()&&version!=state.generation&&!state.importJob.valid()) {
        const auto folder=state.root;const bool native=state.nativeSelectEnabled;state.loadingGeneration=version;
        state.importJob=std::async(std::launch::async,[folder,native] {
            auto result=std::make_shared<std::vector<Asset>>();LoadPlaylists(folder,*result);if(native)LoadNativeSelect(folder,*result);return result;
        });
    }
}
// Cache untouched original stage resources when first visited. This runs on
// the DVD worker, not the emulation thread. Originals are never overwritten.
inline void CaptureOriginal(const DiscIO::Volume& volume,const DiscIO::Partition& partition,
                            const DiscIO::FileInfo& file,const std::string& name)
{
    if(name!="bgm.srt"&&!name.ends_with(".ssd"))return;
    if(name!="bgm.srt")try {MappedEntry(name.substr(0,name.size()-4));}catch(...){return;}
    auto& state=Data();std::filesystem::path root;
    {std::lock_guard lock(state.mutex);root=state.root;}
    if(root.empty()||file.GetSize()==0||file.GetSize()>32u*1024u*1024u)return;
    const auto destination=root/"originals"/name;
    if(std::filesystem::exists(destination))return;
    std::vector<std::uint8_t> data(static_cast<std::size_t>(file.GetSize()));
    if(!volume.Read(file.GetOffset(),file.GetSize(),data.data(),partition))return;
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary=destination.string()+".capturing";
    {std::ofstream out(temporary,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());if(!out)return;}
    std::filesystem::rename(temporary,destination);
}
inline bool Advance(const std::string& resource,std::span<std::uint8_t> mem1,std::span<std::uint8_t> mem2)
{
    auto& state=Data();std::lock_guard lock(state.mutex);
    if(!state.enabled.load()||!state.assets||!state.metadataReady.load())return false;
    // A prepared pool stays private until all validations and allocations finish.
    const bool installing=bool(state.prepared)&&resource=="*";
    auto next=installing?state.prepared:state.assets;
    auto meta=std::find_if(next->begin(),next->end(),[](const Asset& a){return a.name=="bgm.srt";});
    auto oldMeta=std::find_if(state.assets->begin(),state.assets->end(),[](const Asset& a){return a.name=="bgm.srt";});
    if(meta==next->end()||oldMeta==state.assets->end()||meta->original!=oldMeta->original)return false;
    auto merged=oldMeta->replacements[oldMeta->selected];
    struct Change {Asset* audio;std::size_t pick;std::vector<std::uint8_t*> resident;};
    std::vector<Change> changes;
    static std::mt19937 random(std::random_device{}());
    for(auto& audio:*next) {
        if(audio.decoderEntries.empty()||(resource!="*"&&resource!=audio.name))continue;
        const auto target=SrtEntry(merged,audio.entryIndex);
        const std::vector<std::uint8_t> oldEntry(merged.begin()+target,merged.begin()+target+144);
        const auto candidates=audio.selectable?audio.selectable:audio.decoderEntries.size();
        auto oldPick=audio.decoderEntries.size();
        for(std::size_t i=0;i<audio.decoderEntries.size();++i)if(audio.decoderEntries[i]==oldEntry){oldPick=i;break;}
        // A removed source may still be playing. Retain it for this transition;
        // future scans omit it once another song has been selected.
        if(oldPick==audio.decoderEntries.size()) {
            const auto oldAudio=std::find_if(state.assets->begin(),state.assets->end(),[&](const Asset& a){return a.name==audio.name;});
            if(oldAudio==state.assets->end())return false;
            oldPick=audio.decoderEntries.size();audio.decoderEntries.push_back(oldEntry);
            audio.tracks.push_back(oldAudio->track);audio.labels.push_back(TrackLabel(*oldAudio,oldAudio->selected));audio.replacements.push_back(oldAudio->replacements[oldAudio->selected]);
        }
        if(candidates==1&&oldPick==0){audio.selected=oldPick;audio.track=audio.tracks[oldPick];continue;}
        std::size_t pick=(oldPick+1)%candidates;
        if(audio.randomize) {
            std::uniform_int_distribution<std::size_t> choose(0,candidates-1-(oldPick<candidates?1:0));
            pick=choose(random);if(oldPick<candidates&&pick>=oldPick)++pick;
        }
        Change change{&audio,pick,{}};
        for(auto bank:{mem1,mem2}) {
            auto begin=bank.begin();
            while(begin!=bank.end()) {
                auto found=std::search(begin,bank.end(),oldEntry.begin()+32,oldEntry.begin()+64);
                if(found==bank.end())break;
                if(found-bank.begin()>=32&&bank.end()-found>=112&&std::equal(oldEntry.begin(),oldEntry.end(),found-32))
                    change.resident.push_back(&*(found-32));
                begin=found+32;
                if(change.resident.size()>8){Record(state,"Match switch refused: too many resident decoder copies");return false;}
            }
        }
        if(change.resident.empty()) {
            if(!installing)continue;
            // A not-yet-loaded slot keeps its current/original metadata, then
            // participates once that table is resident at a later boundary.
            audio.selected=oldPick;audio.track=audio.tracks[oldPick];continue;
        }
        std::copy_n(audio.decoderEntries[pick].begin(),144,merged.begin()+target);
        changes.push_back(std::move(change));
    }
    if(changes.empty()&&!installing){Record(state,"Match switch waiting: selected decoder metadata not found in RAM");return false;}
    const auto battleCodecs=BuildBattleCodecs(*next);
    // Preallocate status strings before writing guest memory.
    std::vector<std::string> messages;
    for(const auto& change:changes)messages.push_back("Match switch committed: "+change.audio->name+" -> "+TrackLabel(*change.audio,change.pick)+"; resident stereo decoder/loop copies "+std::to_string(change.resident.size()));
    for(auto& change:changes) {
        change.audio->selected=change.pick;change.audio->track=change.audio->tracks[change.pick];
        for(auto* at:change.resident)std::copy_n(change.audio->decoderEntries[change.pick].begin(),144,at);
    }
    meta->replacements[meta->selected]=std::move(merged);
    ContinuoCharacterMusic::Data().codecs.store(battleCodecs);
    state.assets=std::move(next);if(installing)state.prepared.reset();
    std::fill(state.metadataCoverage.begin(),state.metadataCoverage.end(),1);state.metadataPending=0;
    for(const auto& message:messages)Record(state,message);
    if(installing)Record(state,"Imported playlists activated between matches");
    return true;
}
// An older DVD result can still be queued when the loading boundary switches.
// Correct matching SRT bytes at delivery on the CPU thread, before the game
// receives its completion interrupt. Verify all immutable bytes first.
inline void RefreshDeliveredMetadata(const DiscIO::Volume& volume,const DiscIO::Partition& partition,
                                     std::uint64_t offset,std::span<std::uint8_t> bytes)
{
    auto& state=Data();if(!state.enabled.load()||bytes.empty())return;
    const auto* fs=volume.GetFileSystem(partition);if(!fs)return;
    const auto file=fs->FindFileInfo(offset);if(!file||offset<file->GetOffset())return;
    const auto path=file->GetPath();const auto slash=path.find_last_of('/');
    if(path.substr(slash==std::string::npos?0:slash+1)!="bgm.srt")return;
    std::lock_guard lock(state.mutex);if(!state.assets)return;
    const auto meta=std::find_if(state.assets->begin(),state.assets->end(),[](const Asset& a){return a.name=="bgm.srt";});
    if(meta==state.assets->end()||file->GetSize()!=meta->original.size())return;
    const auto at=std::size_t(offset-file->GetOffset());if(at>=meta->original.size())return;
    const auto count=(std::min)(bytes.size(),meta->original.size()-at);
    std::vector<std::uint8_t> mutableBytes(count,0);
    for(const auto& audio:*state.assets)if(!audio.decoderEntries.empty()) {
        const auto entry=SrtEntry(meta->original,audio.entryIndex);
        const auto start=(std::max)(at,entry),end=(std::min)(at+count,entry+144);
        if(start>=end)continue;
        bool matches=false;
        for(const auto& candidate:audio.decoderEntries)
            if(std::equal(bytes.begin()+start-at,bytes.begin()+end-at,candidate.begin()+start-entry))matches=true;
        if(!matches)return;
        std::fill(mutableBytes.begin()+start-at,mutableBytes.begin()+end-at,1);
    }
    for(std::size_t i=0;i<count;++i)if(!mutableBytes[i]&&bytes[i]!=meta->original[at+i])return;
    std::copy_n(meta->replacements[meta->selected].begin()+at,count,bytes.begin());
}
inline bool Apply(const DiscIO::Volume& volume,const DiscIO::Partition& partition,std::uint64_t offset,std::vector<std::uint8_t>& buffer)
{
    auto& state=Data();if(!state.enabled.load(std::memory_order_acquire)||buffer.empty())return false;
    const auto* filesystem=volume.GetFileSystem(partition);if(!filesystem)return false;
    const auto file=filesystem->FindFileInfo(offset);if(!file||offset<file->GetOffset())return false;
    const auto relative=offset-file->GetOffset();const auto path=file->GetPath();
    const auto slash=path.find_last_of('/');const auto name=path.substr(slash==std::string::npos?0:slash+1);
    try {
        CaptureOriginal(volume,partition,*file,name);
        if(name.ends_with(".ssd")){ContinuoStageMusic::Request(static_cast<unsigned>(MappedEntry(name.substr(0,name.size()-4))));ContinuoStageMusic::Poll();}
    } catch (...) {}
    if(name=="bgm.srt" && relative==0) {
        try {if(const auto codecs=ContinuoCharacterMusic::Data().codecs.load();!codecs||codecs->empty())
            ContinuoCharacterMusic::Data().codecs.store(BuildBattleCodecs({},state.root));} catch (...) {}
    }
    std::lock_guard lock(state.mutex);
    if(!state.assets)return false;
    for(const auto& asset:*state.assets) {
        auto fullPath=path;while(!fullPath.empty()&&fullPath.front()=='/')fullPath.erase(fullPath.begin());
        if(asset.name.find('/')!=std::string::npos?fullPath!=asset.name:name!=asset.name)continue;
        if(file->GetSize()!=asset.original.size()) {
            Record(state,"Bypassed "+name+": disc size differs from installed original");return false;
        }
        if(relative>=asset.original.size())return false;
        const bool metadata=asset.name=="bgm.srt";
        const auto at=static_cast<std::size_t>(relative);
        // DVD requests may split a file or include padding after its last byte.
        // Verify and replace just this file's intersection, preserving padding
        // and any following file bytes in the original request.
        const auto count=(std::min)(buffer.size(),asset.original.size()-at);
        if(!std::equal(buffer.begin(),buffer.begin()+count,asset.original.begin()+at)) {
            Record(state,"Bypassed "+name+": original bytes differ at read offset "+std::to_string(at));return false;
        }
        if(asset.name.ends_with(".ssd") && !state.metadataReady.load(std::memory_order_acquire)) {
            Record(state,"Waiting for bgm.srt before "+name+"; decoder metadata has not been served yet");return false;
        }
        const auto& replacement=asset.replacements[asset.selected];
        std::copy_n(replacement.begin()+at,count,buffer.begin());
        if(metadata) {
            for(std::size_t i=at;i<at+count;++i)if(!state.metadataCoverage[i]) {
                state.metadataCoverage[i]=1;--state.metadataPending;
            }
            if(state.metadataPending==0) {
                state.metadataReady.store(true,std::memory_order_release);
                Record(state,"bgm.srt decoder and loop metadata served; audio replacement ready");
            }
        } else {
            Record(state,"Serving "+name+" with its selected decoder and loop metadata");
            // Advance once after this stage actually starts streaming, never on
            // Configure alone, refills, loops, or restarts of the same stream.
            if(!asset.rotationPath.empty()&&std::find(state.rotationCommitted.begin(),state.rotationCommitted.end(),name)==state.rotationCommitted.end()) {
                state.rotationCommitted.push_back(name);
                std::ofstream cursor(asset.rotationPath,std::ios::trunc);
                if(cursor) {cursor<<asset.nextRotation<<'\n';cursor.flush();}
                Record(state,cursor?"Saved next song for "+name:"Could not save next song for "+name+"; this choice may repeat on the next boot");
            }
        }
        return true;
    }
    return false;
}
}
