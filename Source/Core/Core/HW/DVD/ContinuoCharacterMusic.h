#pragma once
// Secondary character music: replace only recognized battle BGM voices at the
// native 32 kHz AX mixer output. Guest stream/decoder state continues normally.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <vector>
namespace ContinuoCharacterMusic {
struct Codec {std::array<std::int16_t,16> coefficients{};unsigned channel=0,entry=0;};
struct Song {std::string name,title;std::vector<std::int16_t> pcm;};
using Library=std::map<unsigned,std::vector<std::shared_ptr<const Song>>>;
struct Playback {std::shared_ptr<const Song> song;std::uint64_t serial=0;};
struct State {
    std::mutex mutex;
    std::filesystem::path root;
    std::string version,loading;
    std::future<std::shared_ptr<const Library>> job;
    std::shared_ptr<const Library> library;
    std::atomic<std::shared_ptr<const std::vector<Codec>>> codecs;
    std::atomic<std::shared_ptr<const Playback>> playback;
    std::map<unsigned,std::string> last;
    std::uint64_t serial=0;
    std::atomic<std::uint64_t> mixed{0};
    std::string status="Character folders ready; waiting for imported songs.";
};
inline State& Data(){static State s;return s;}
inline std::string Version(const std::filesystem::path& root)
{std::ifstream file(root/"characters/version.txt");std::string s;file>>s;return s;}
inline void AddEntry(std::vector<Codec>& out,std::span<const std::uint8_t> entry,unsigned resource=0)
{
    if(entry.size()!=144)return;
    for(unsigned channel=0;channel<2;++channel) {
        Codec c;c.channel=channel;c.entry=resource;bool any=false;
        for(unsigned i=0;i<16;++i){const auto at=32+64*channel+2*i;c.coefficients[i]=static_cast<std::int16_t>((unsigned(entry[at])<<8)|entry[at+1]);any|=c.coefficients[i]!=0;}
        if(any&&std::none_of(out.begin(),out.end(),[&](const Codec& old){return old.entry==resource&&old.channel==channel&&old.coefficients==c.coefficients;}))out.push_back(c);
    }
}
inline void SetCodecs(std::vector<Codec> codecs)
{Data().codecs.store(std::make_shared<const std::vector<Codec>>(std::move(codecs)));}
inline std::shared_ptr<const Library> Load(const std::filesystem::path& root)
{
    auto result=std::make_shared<Library>();std::size_t total=0;
    for(unsigned id=1;id<=30;++id) {
        const auto folder=root/"characters"/std::to_string(id);
        std::ifstream sequence(folder/"sequence.txt");std::string name;
        while(sequence>>name) {
            if((*result)[id].size()>=63||name.rfind("Song_",0)!=0||name.find_first_of("/\\:")!=std::string::npos||name.find("..")!=std::string::npos)throw std::runtime_error("Invalid character sequence");
            std::ifstream file(folder/name/"stereo.pcm",std::ios::binary|std::ios::ate);
            if(!file)throw std::runtime_error("Missing character PCM");
            const auto size=file.tellg();
            if(size<=0||size%4||size>128u*1024u*1024u||total+std::size_t(size)>512u*1024u*1024u)throw std::runtime_error("Character library exceeds 512 MiB or has invalid PCM");
            total+=std::size_t(size);auto song=std::make_shared<Song>();song->name=name;
            song->pcm.resize(std::size_t(size)/2);file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(song->pcm.data()),size))throw std::runtime_error("Incomplete character PCM");
            // Runtime targets are little endian; decode explicitly for portability.
            auto* bytes=reinterpret_cast<const unsigned char*>(song->pcm.data());
            for(std::size_t i=0;i<song->pcm.size();++i)song->pcm[i]=static_cast<std::int16_t>(unsigned(bytes[2*i])|(unsigned(bytes[2*i+1])<<8));
            std::ifstream title(folder/name/"title.txt");std::getline(title,song->title);if(song->title.empty())song->title=name;
            (*result)[id].push_back(std::move(song));
        }
    }
    return result;
}
inline void Configure(const std::filesystem::path& root)
{
    auto& s=Data();std::lock_guard lock(s.mutex);
    if(s.job.valid())s.job.wait();
    s.job={};s.root=root;s.version.clear();s.loading.clear();
    s.library.reset();s.last.clear();s.playback.store(nullptr);s.codecs.store(nullptr);s.mixed.store(0);s.status="Character folders ready; waiting for imported songs.";
}
inline void Poll()
{
    auto& s=Data();std::lock_guard lock(s.mutex);if(s.root.empty())return;
    if(s.job.valid()&&s.job.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {auto library=s.job.get();if(Version(s.root)==s.loading){s.library=std::move(library);s.version=s.loading;s.status="Character playlists loaded; ordered by filename unless Random is enabled.";}}
        catch(const std::exception& e){s.version=s.loading;s.status=std::string("Character load failed: ")+e.what();}
    }
    const auto version=Version(s.root);
    if(!version.empty()&&version!=s.version&&!s.job.valid()) {
        s.loading=version;const auto root=s.root;s.job=std::async(std::launch::async,[root]{return Load(root);});
    }
}
inline bool Select(unsigned character,bool randomize)
{
    auto& s=Data();std::lock_guard lock(s.mutex);
    if(!character){s.playback.store(nullptr);return true;}
    if(!s.library)return false;
    const auto it=s.library->find(character);if(it==s.library->end()||it->second.empty()){s.playback.store(nullptr);s.status="No imported songs for character "+std::to_string(character)+"; stage music continues.";return true;}
    const auto codecs=s.codecs.load();if(!codecs||codecs->empty()){s.status="Character playback waiting for battle music decoder signatures.";return false;}
    const auto& songs=it->second;std::size_t previous=songs.size();
    for(std::size_t i=0;i<songs.size();++i)if(songs[i]->name==s.last[character])previous=i;
    std::size_t pick=previous<songs.size()?(previous+1)%songs.size():0;
    if(randomize) {
        static std::mt19937 random(std::random_device{}());
        const bool avoid=songs.size()>1&&previous<songs.size();
        std::uniform_int_distribution<std::size_t> choose(0,songs.size()-1-(avoid?1:0));pick=choose(random);
        if(avoid&&pick>=previous)++pick;
    }
    auto playback=std::make_shared<Playback>();playback->song=songs[pick];playback->serial=++s.serial;
    s.last[character]=songs[pick]->name;s.mixed.store(0);s.playback.store(std::move(playback));
    s.status="Character "+std::to_string(character)+": "+songs[pick]->title+(randomize?" (random)":" (in order)");return true;
}
inline std::string Status()
{auto& s=Data();std::lock_guard lock(s.mutex);return s.status+" | replaced BGM samples: "+std::to_string(s.mixed.load());}
// Implemented by the unified stage/character mixer after this namespace.
inline bool Mix(const std::int16_t*,unsigned,unsigned,unsigned,std::span<std::int16_t>);
}
#include "ContinuoStageMusic.h"
namespace ContinuoCharacterMusic {
inline bool Mix(const std::int16_t*,unsigned,unsigned,unsigned,std::span<std::int16_t> samples)
{return ContinuoMusicMixer::Mix(samples);}
}
