#pragma once
bool TVCRollbackReplayModeEnabled();
// Host-only stage catalog and PCM playback. Native SSD/SRT bytes stay unchanged.
// Included by ContinuoCharacterMusic.h after its shared types.
namespace ContinuoStageMusic {
using Song=ContinuoCharacterMusic::Song;
struct Playback {std::shared_ptr<const Song> song;unsigned entry=0;std::uint64_t serial=0;};
struct State {
    std::mutex mutex;
    std::filesystem::path root;
    std::future<std::shared_ptr<Playback>> job;
    std::atomic<std::shared_ptr<const Playback>> playback;
    std::atomic<unsigned> requested{0};
    std::atomic<bool> enabled{true};
    bool randomize=true,battleActive=false;
    std::map<unsigned,std::string> last;
    std::uint64_t epoch=0,loadingEpoch=0,serial=0;
    unsigned loadingEntry=0,attemptedEntry=0;
    std::string attemptedVersion;
    std::string status="Stage PCM ready; waiting for a native music resource.";
};
inline State& Data(){static State s;return s;}
inline void Configure(const std::filesystem::path& root)
{
    auto& s=Data();std::lock_guard lock(s.mutex);if(s.job.valid())s.job.wait();
    s.job={};s.root=root;s.last.clear();s.epoch++;s.serial++;s.loadingEntry=s.attemptedEntry=0;
    s.requested.store(0);s.playback.store(nullptr);s.enabled.store(!root.empty());s.attemptedVersion.clear();
    s.status="Stage PCM ready; waiting for a native music resource.";
}
inline bool Battle(unsigned entry){return (entry>=14&&entry<=16)||(entry>=30&&entry<=44);}
inline void Request(unsigned entry)
{if(entry>=1&&entry<=44&&Data().enabled.load())Data().requested.store(entry);}
inline void Control(bool enabled,bool randomize,bool restart,bool battleActive)
{
    auto& s=Data();std::lock_guard lock(s.mutex);s.enabled.store(enabled);s.randomize=randomize;s.battleActive=battleActive;
    if(!enabled||restart){++s.epoch;s.attemptedEntry=0;s.attemptedVersion.clear();s.playback.store(nullptr);}
    if(!enabled||restart)s.requested.store(0);
}
inline std::shared_ptr<Playback> Load(const std::filesystem::path& root,unsigned entry,
                                    const std::string& previous,bool randomize)
{
    const auto folder=root/"pcm/stages"/std::to_string(entry);
    std::ifstream sequence(folder/"sequence.txt");std::string name;std::vector<std::string> names;
    while(sequence>>name){
        if(names.size()>=10000||name.rfind("Song_",0)!=0||name.find_first_of("/\\:")!=std::string::npos||name.find("..")!=std::string::npos)
            throw std::runtime_error("Invalid stage PCM sequence");
        if(std::find(names.begin(),names.end(),name)!=names.end())continue;
        names.push_back(name);
    }
    auto result=std::make_shared<Playback>();result->entry=entry;
    if(names.empty())return result;
    std::size_t before=names.size();for(std::size_t i=0;i<names.size();++i)if(names[i]==previous)before=i;
    std::size_t pick=before<names.size()?(before+1)%names.size():0;
    if(randomize){
        std::mt19937 rng(std::random_device{}());const bool avoid=before<names.size()&&names.size()>1;
        std::uniform_int_distribution<std::size_t> choose(0,names.size()-1-(avoid?1:0));pick=choose(rng);if(avoid&&pick>=before)++pick;
    }
    std::ifstream file(folder/names[pick]/"stereo.pcm",std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Missing stage PCM: "+names[pick]);
    const auto size=file.tellg();
    if(size<=0||size%4||size>128u*1024u*1024u)throw std::runtime_error("Invalid stage PCM size");
    auto song=std::make_shared<Song>();song->name=names[pick];song->pcm.resize(std::size_t(size)/2);
    file.seekg(0);if(!file.read(reinterpret_cast<char*>(song->pcm.data()),size))throw std::runtime_error("Incomplete stage PCM");
    auto* bytes=reinterpret_cast<const unsigned char*>(song->pcm.data());
    for(std::size_t i=0;i<song->pcm.size();++i)song->pcm[i]=static_cast<std::int16_t>(unsigned(bytes[2*i])|(unsigned(bytes[2*i+1])<<8));
    std::ifstream title(folder/names[pick]/"title.txt");std::getline(title,song->title);if(song->title.empty())song->title=song->name;
    result->song=std::move(song);return result;
}
inline void Poll()
{
    auto& s=Data();std::lock_guard lock(s.mutex);if(s.root.empty())return;
    if(s.job.valid()&&s.job.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        try {auto p=s.job.get();if(s.enabled.load()&&s.loadingEpoch==s.epoch&&p->entry==s.requested.load()){
            p->serial=++s.serial;if(p->song){s.last[p->entry]=p->song->name;s.status="Stage PCM "+std::to_string(p->entry)+": "+p->song->title+" | 32000 Hz stereo";}
            else s.status="No PCM songs for resource "+std::to_string(p->entry)+"; native music continues.";
            s.playback.store(std::move(p));}}
        catch(const std::exception& e){s.status=std::string("Stage PCM load failed: ")+e.what();}
    }
    const auto entry=s.requested.load();if(!s.enabled.load()||!entry||s.job.valid())return;
    std::ifstream versionFile(s.root/"pcm/version.txt");std::string version;versionFile>>version;
    const auto current=s.playback.load();
    // Published catalogs affect the next instance, never replace an active song.
    if(current&&current->entry==entry&&current->song)return;
    if(entry==s.attemptedEntry&&version==s.attemptedVersion)return;
    s.attemptedEntry=entry;s.attemptedVersion=version;s.loadingEntry=entry;s.loadingEpoch=s.epoch;
    const auto root=s.root;const auto previous=s.last[entry];const auto randomize=s.randomize;
    s.status="Loading stage PCM resource "+std::to_string(entry)+" in the background.";
    s.job=std::async(std::launch::async,[root,entry,previous,randomize]{return Load(root,entry,previous,randomize);});
}
inline std::string Status(){auto& s=Data();std::lock_guard lock(s.mutex);return s.status;}
}

// One owner and one stereo timeline for every AX Wii output block.
namespace ContinuoMusicMixer {
inline std::atomic<unsigned>& OutputMode(){static std::atomic<unsigned> mode{1};return mode;}
inline void SetOutputMode(unsigned mode){OutputMode().store(mode<=2?mode:0,std::memory_order_relaxed);}
struct Block {
    std::shared_ptr<const ContinuoCharacterMusic::Playback> character;
    std::shared_ptr<const ContinuoStageMusic::Playback> stage;
    std::shared_ptr<const ContinuoCharacterMusic::Song> song;
    std::shared_ptr<const std::vector<ContinuoCharacterMusic::Codec>> codecs;
    std::array<unsigned,2> voices{};
    std::array<unsigned,2> candidates{};
    unsigned entry=0,voice=0,offset=0,seen=0;
    bool ambiguous=false,ready=false,characterOwner=false;
    std::uint64_t serial=0,cursorSerial=0;
    bool cursorCharacter=false;
    std::size_t frame=0;
};
inline Block& Current(){static thread_local Block b;return b;}
inline void BeginBlock()
{
    auto& b=Current();b.character=ContinuoCharacterMusic::Data().playback.load();b.stage=ContinuoStageMusic::Data().playback.load();
    b.codecs=ContinuoCharacterMusic::Data().codecs.load();b.song.reset();b.voices={};b.candidates={};b.entry=0;b.seen=0;b.ambiguous=false;b.ready=false;b.voice=0;b.offset=0;
}
inline void Observe(unsigned voice,unsigned running,const std::int16_t* coefficients,
                    unsigned format,unsigned leftVolume,unsigned rightVolume)
{
    auto& b=Current();if(running!=1||format!=0||!b.codecs)return;
    unsigned entry=0;int channel=-1;bool left=false,right=false;
    for(const auto& codec:*b.codecs)if(std::equal(codec.coefficients.begin(),codec.coefficients.end(),coefficients)){
        if(entry&&entry!=codec.entry){b.ambiguous=true;return;}
        entry=codec.entry;if(codec.channel==0)left=true;else right=true;
    }
    if(!entry)return;
    if(left&&right){if(leftVolume==rightVolume){b.ambiguous=true;return;}channel=leftVolume>rightVolume?0:1;}else channel=left?0:1;
    if(b.entry&&b.entry!=entry){b.ambiguous=true;return;}b.entry=entry;
    if(b.candidates[channel]&&b.candidates[channel]!=voice){b.ambiguous=true;return;}
    b.candidates[channel]=voice;
}
inline void EndDiscovery()
{
    auto& b=Current();if(b.ambiguous||!b.entry||!b.candidates[0]||!b.candidates[1]||b.candidates[0]==b.candidates[1])return;
    const unsigned mode=OutputMode().load(std::memory_order_relaxed);
    if(mode==0)return;
    b.voices=b.candidates;b.ready=true;
    if(mode==2)return;
    ContinuoStageMusic::Request(b.entry);
    b.characterOwner=b.character&&b.character->song&&ContinuoStageMusic::Battle(b.entry);
    if(b.characterOwner){b.song=b.character->song;b.serial=b.character->serial;}
    else if(b.stage&&b.stage->entry==b.entry&&b.stage->song){b.song=b.stage->song;b.serial=b.stage->serial;}
    if(!b.song||b.song->pcm.size()<2){b.ready=false;return;}
    if(b.cursorSerial!=b.serial||b.cursorCharacter!=b.characterOwner){b.frame=0;b.cursorSerial=b.serial;b.cursorCharacter=b.characterOwner;}
}
inline void SetVoice(unsigned voice,unsigned offset){auto& b=Current();b.voice=voice;b.offset=offset;}
inline bool Mix(std::span<std::int16_t> samples)
{
    auto& b=Current();if(!b.ready||b.offset+samples.size()>96)return false;
    int channel=b.voice==b.voices[0]?0:b.voice==b.voices[1]?1:-1;if(channel<0)return false;
    const unsigned mode=OutputMode().load(std::memory_order_relaxed);
    if(mode==2){std::fill(samples.begin(),samples.end(),std::int16_t{0});b.seen|=1u<<channel;return true;}
    if(mode!=1||!b.song||b.song->pcm.size()<2)return false;
    const auto frames=b.song->pcm.size()/2;
    for(std::size_t i=0;i<samples.size();++i)samples[i]=b.song->pcm[((b.frame+b.offset+i)%frames)*2+channel];
    b.seen|=1u<<channel;
    ContinuoCharacterMusic::Data().mixed.fetch_add(samples.size(),std::memory_order_relaxed);return true;
}
inline void EndBlock()
{
    auto& b=Current();
    if(b.ready&&b.seen==3&&b.song&&b.song->pcm.size()>=2&&
       OutputMode().load(std::memory_order_relaxed)==1&&!TVCRollbackReplayModeEnabled())
        b.frame=(b.frame+96)%(b.song->pcm.size()/2);
    b.ready=false;b.song.reset();b.character.reset();b.stage.reset();b.codecs.reset();
}
}
