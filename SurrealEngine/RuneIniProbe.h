#pragma once
#include "Package/IniFile.h"
#include "Utils/File.h"

inline void RunRuneIniProbe(const std::string& path)
{
    // Explicit diagnostic output only; never use a game's live configuration.
    File::write_all_text(path, "; preserve comment\n[RuneI.RunePlayer]\nSavedGameInfo[0]=old\nSavedGameInfo[10]=checkpoint\nPlain=keep\nOdd[abc]=literal\nBindings=first\nBindings=second\n[Other]\nValue=untouched\n");
    IniFile config(path);
    config.SetValue("RuneI.RunePlayer", "SavedGameInfo[0]", "new");
    config.SetValue("RuneI.RunePlayer", "Plain", "updated");
    config.SetValue("RuneI.RunePlayer", "Bindings", "changed-second", 1);
    config.UpdateFile(path);
    config.SetValue("RuneI.RunePlayer", "SavedGameInfo[10]", "checkpoint2");
    config.SetValue("RuneI.RunePlayer", "Plain", "updated2");
    config.UpdateFile(path);
    IniFile loaded(path);
    if (loaded.GetValue("RuneI.RunePlayer", "SavedGameInfo[0]") != "new" ||
        loaded.GetValue("RuneI.RunePlayer", "SavedGameInfo[10]") != "checkpoint2" ||
        loaded.GetValue("RuneI.RunePlayer", "Plain") != "updated2" ||
        loaded.GetValue("RuneI.RunePlayer", "Odd[abc]") != "literal" ||
        loaded.GetValue("RuneI.RunePlayer", "Bindings", "", 0) != "first" ||
        loaded.GetValue("RuneI.RunePlayer", "Bindings", "", 1) != "changed-second" ||
        loaded.GetValue("Other", "Value") != "untouched" ||
        File::read_all_text(path).find("; preserve comment") == std::string::npos)
        throw std::runtime_error("Rune INI bracketed-key round trip failed");
    std::cout << "RUNE_INI_ROUNDTRIP_OK bracketed=2 plain=1 repeated=1 literal_brackets=1 unrelated=1 comment=1 updates=2\n";
}
