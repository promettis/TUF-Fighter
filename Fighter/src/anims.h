// ============================================================================
//  TUF FIGHTER - anims.h
//  The animation library. Every animation is authored in code as a small
//  list of key poses (see pose.h) that are interpolated at 60 fps.
//  All poses are authored for a fighter facing +Z ("player 1 side");
//  the mirrored version for the other side is produced with poseMirror().
// ============================================================================
#pragma once
#include "pose.h"

enum AnimId
{
    ANIM_IDLE, ANIM_WALK_F, ANIM_WALK_B, ANIM_CROUCH, ANIM_JUMP,
    ANIM_BLOCK, ANIM_BLOCK_CROUCH,
    ANIM_HIT_HIGH, ANIM_HIT_LOW, ANIM_HIT_CROUCH,
    ANIM_FALL, ANIM_DOWN, ANIM_GETUP, ANIM_WIN, ANIM_INTRO,
    // attacks
    ANIM_LP, ANIM_HP, ANIM_LK, ANIM_HK,
    ANIM_CLP, ANIM_CHP, ANIM_CLK, ANIM_CHK,
    ANIM_JP, ANIM_JK,
    ANIM_FHP, ANIM_FHK, ANIM_DIVE,
    // special moves
    ANIM_FIREBALL, ANIM_RUSH, ANIM_RISE, ANIM_BEAM,
    ANIM_COUNT
};

void  animInit();                          // builds the key tables (call once)
Pose  animEval(AnimId id, float frame);    // pose of an animation at a frame (60 fps)
float animLength(AnimId id);               // length in frames
bool  animLoops(AnimId id);
const char* animName(AnimId id);
