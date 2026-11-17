// ============================================================================
//  TUF FIGHTER - audio.h
//  All sound effects and the music are synthesised in code at start-up,
//  so the game needs no audio files. Dropping a file with the right name
//  into Assets/Sounds replaces the built-in sound.
// ============================================================================
#pragma once
#include "engine.h"

enum Sfx
{
    SFX_UI_MOVE, SFX_UI_SELECT, SFX_UI_BACK,
    SFX_WHOOSH_LIGHT, SFX_WHOOSH_HEAVY, SFX_HIT_LIGHT, SFX_HIT_HEAVY, SFX_BLOCK,
    SFX_JUMP, SFX_LAND, SFX_BODY_FALL, SFX_KO, SFX_ROUND, SFX_FIGHT, SFX_VS, SFX_WIN,
    SFX_SPECIAL, SFX_CHARGE, SFX_BEAM, SFX_SHOT, SFX_EXPLODE,
    SFX_COUNT
};
enum MusicTrack { MUSIC_NONE, MUSIC_MENU, MUSIC_FIGHT };

// `soundDir` may contain replacement files (see Assets/Sounds/README.txt); anything missing is synthesised
void audioInit(const std::string& soundDir);
void audioShutdown();
void audioUpdate(float dt);
void sfxPlay(Sfx id, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
void musicPlay(MusicTrack t);
void musicDuck(float amount);          // 0 = normal, 1 = silent (used during K.O.)
void audioToggleMute();
bool audioMuted();
