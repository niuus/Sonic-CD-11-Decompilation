#ifndef MOD_API_H
#define MOD_API_H

#if RETRO_USE_MOD_LOADER
#include <string>
#include <map>
#include <unordered_map>
#if RETRO_PLATFORM == RETRO_WII
#include "tinyxml2.h"
#else
#include <tinyxml2.h>
#endif

#define PLAYER_MAX (0x10)

struct ModInfo {
    std::string name;
    std::string desc;
    std::string author;
    std::string version;
    std::map<std::string, std::string> fileMap;
    std::string folder;
    bool useScripts;
    bool disableFocusPause;
    bool redirectSave;
    bool disableSaveIniOverride;
    std::string savePath;
    bool active;
};

extern std::vector<ModInfo> modList;
extern int activeMod;

extern char modsPath[0x100];

extern bool redirectSave;
extern bool disableSaveIniOverride;

extern char modTypeNames[OBJECT_COUNT][0x40];
extern char modScriptPaths[OBJECT_COUNT][0x40];
extern byte modScriptFlags[OBJECT_COUNT];
extern byte modObjCount;

extern char playerNames[PLAYER_MAX][0x20];
extern byte playerCount;

inline void SetActiveMod(int id) { activeMod = id; }

void initMods();
bool loadMod(ModInfo *info, std::string modsPath, std::string folder, bool active);
void scanModFolder(ModInfo *info);
void saveMods();

int OpenModMenu();

void RefreshEngine();

#endif

#if RETRO_USE_MOD_LOADER || !RETRO_USE_ORIGINAL_CODE
extern char savePath[0x100];
int GetSceneID(byte listID, const char *sceneName);
#endif

#endif