#pragma once
#include <filesystem>
namespace ContinuoAssetPaths {
// The override API still receives the music directory for audio assets. Native
// character-select archives live alongside music and retain a legacy fallback.
inline std::filesystem::path NativeSelect(const std::filesystem::path& musicOverrides)
{
    const auto native = musicOverrides.parent_path().parent_path() / "character-select" / "native";
    if (std::filesystem::exists(native / "menu.fpk"))
        return native;
    return musicOverrides / "native-select";
}
}
