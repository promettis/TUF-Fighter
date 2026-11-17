// ============================================================================
//  TUF FIGHTER - fighter.cpp
// ============================================================================
#include "fighter.h"

// ---------------------------------------------------------------------------
// Move table (frame data). Frame numbers match the key poses in anims.cpp.
// ---------------------------------------------------------------------------
//   name            anim        startup active total dmg hitstun blockstun pushHit pushBlock level   limb            radius  kd     lvx   lvy  stop lunge rank heavy chip noScale special
static const MoveDef MOVES[MV_COUNT] = {
    { "Jab",          ANIM_LP,       4,  3, 15,  40, 15, 10, 2.0f, 2.2f, HL_MID,      LIMB_LEAD_HAND, 0.12f, false, 2.6f, 4.2f,  5, 0.4f, 0, false, 0.0f, false, false },
    { "Straight",     ANIM_HP,       9,  4, 30,  95, 22, 15, 3.4f, 3.2f, HL_MID,      LIMB_REAR_HAND, 0.13f, false, 3.4f, 4.6f,  9, 1.3f, 2, true,  0.0f, false, false },
    { "Snap Kick",    ANIM_LK,       6,  3, 22,  55, 17, 11, 2.6f, 2.6f, HL_MID,      LIMB_LEAD_FOOT, 0.13f, false, 2.8f, 4.2f,  6, 0.2f, 1, false, 0.0f, false, false },
    { "Roundhouse",   ANIM_HK,      12,  4, 38, 120, 24, 17, 4.0f, 3.6f, HL_MID,      LIMB_REAR_FOOT, 0.15f, true,  4.0f, 4.4f, 11, 0.5f, 3, true,  0.0f, false, false },
    { "Low Jab",      ANIM_CLP,      4,  3, 14,  35, 14,  9, 1.8f, 2.0f, HL_MID,      LIMB_LEAD_HAND, 0.12f, false, 2.4f, 4.0f,  5, 0.0f, 0, false, 0.0f, false, false },
    { "Uppercut",     ANIM_CHP,      7,  6, 34, 100, 24, 17, 1.2f, 3.0f, HL_MID,      LIMB_REAR_HAND, 0.16f, true,  1.3f, 8.4f, 10, 0.9f, 2, true,  0.0f, false, false },
    { "Low Kick",     ANIM_CLK,      5,  3, 20,  45, 15, 10, 2.0f, 2.2f, HL_LOW,      LIMB_LEAD_FOOT, 0.12f, false, 2.4f, 4.0f,  5, 0.0f, 1, false, 0.0f, false, false },
    { "Sweep",        ANIM_CHK,     10,  4, 38,  90, 24, 17, 2.0f, 3.2f, HL_LOW,      LIMB_REAR_FOOT, 0.13f, true,  1.8f, 3.6f,  9, 0.3f, 3, true,  0.0f, false, false },
    { "Jump Punch",   ANIM_JP,       5, 18, 60,  70, 20, 14, 2.4f, 2.6f, HL_OVERHEAD, LIMB_REAR_HAND, 0.15f, false, 2.8f, 4.0f,  7, 0.0f, 0, false, 0.0f, false, false },
    { "Jump Kick",    ANIM_JK,       6, 20, 60,  80, 22, 15, 2.8f, 2.8f, HL_OVERHEAD, LIMB_LEAD_FOOT, 0.16f, false, 3.0f, 4.2f,  8, 0.0f, 0, true,  0.0f, false, false },
    { "Overhead",     ANIM_FHP,     18,  4, 40, 105, 24, 14, 2.6f, 2.8f, HL_OVERHEAD, LIMB_REAR_HAND, 0.16f, false, 2.6f, 4.0f, 10, 0.9f, 2, true,  0.0f, false, false },
    { "Push Kick",    ANIM_FHK,     11,  4, 36,  90, 22, 16, 6.2f, 4.6f, HL_MID,      LIMB_REAR_FOOT, 0.15f, false, 4.4f, 4.0f, 10, 0.6f, 3, true,  0.0f, false, false },
    { "Dive Kick",    ANIM_DIVE,     6, 60, 90,  75, 20, 12, 2.6f, 2.8f, HL_OVERHEAD, LIMB_LEAD_FOOT, 0.17f, false, 3.0f, 4.2f,  8, 0.0f, 0, true,  0.0f, false, true  },
    { "Energy Shot",  ANIM_FIREBALL,13,  0, 42,   0,  0,  0, 0.0f, 0.0f, HL_MID,      LIMB_LEAD_HAND, 0.00f, false, 0.0f, 0.0f,  0, 0.0f, 5, true,  0.0f, false, true  },
    { "Rush Punch",   ANIM_RUSH,    12,  7, 44, 115, 24, 16, 3.0f, 3.6f, HL_MID,      LIMB_REAR_HAND, 0.18f, true,  5.0f, 4.2f, 12, 0.0f, 5, true,  0.2f, false, true  },
    { "Rising Upper", ANIM_RISE,     5, 13, 54, 130, 24, 18, 1.5f, 3.2f, HL_MID,      LIMB_REAR_HAND, 0.21f, true,  1.6f, 9.0f, 12, 0.0f, 5, true,  0.2f, false, true  },
    { "Super Beam",   ANIM_BEAM,    46, 54, 126,  0,  0,  0, 0.0f, 0.0f, HL_MID,      LIMB_LEAD_HAND, 0.00f, false, 0.0f, 0.0f,  0, 0.0f, 6, true,  0.0f, true,  true  },
};
const MoveDef& moveDef(MoveId id) { return MOVES[id]; }

static const MoveDef SHOT_HIT  = { "Energy Shot", ANIM_FIREBALL, 0, 0, 0, 80, 22, 15, 3.4f, 3.0f, HL_MID, LIMB_LEAD_HAND, 0.0f, false, 3.4f, 4.4f, 9, 0.0f, 5, true, 0.25f, false, true };
static const MoveDef BEAM_HIT  = { "Super Beam",  ANIM_BEAM,     0, 0, 0, 30, 20, 16, 2.6f, 2.4f, HL_MID, LIMB_LEAD_HAND, 0.0f, false, 3.0f, 2.6f, 2, 0.0f, 6, true, 0.25f, true,  true };
static const MoveDef BEAM_LAST = { "Super Beam",  ANIM_BEAM,     0, 0, 0, 40, 24, 18, 4.0f, 3.6f, HL_MID, LIMB_LEAD_HAND, 0.0f, true,  6.0f, 5.4f, 12, 0.0f, 6, true, 0.25f, true,  true };
const MoveDef& projectileHitDef() { return SHOT_HIT; }
const MoveDef& beamHitDef(bool last) { return last ? BEAM_LAST : BEAM_HIT; }

static bool isCrouchMove(MoveId m) { return m == MV_CLP || m == MV_CLK || m == MV_CHK; }

MoveId Fighter::pickGroundMove(Button b, const InputState& input) const
{
    bool fwd = facing > 0 ? input.right : input.left;
    bool crouching = input.down;
    switch (b)
    {
    case BTN_LP: return crouching ? MV_CLP : MV_LP;
    case BTN_HP: return crouching ? MV_CHP : (fwd ? MV_FHP : MV_HP);
    case BTN_LK: return crouching ? MV_CLK : MV_LK;
    case BTN_HK: return crouching ? MV_CHK : (fwd ? MV_FHK : MV_HK);
    case BTN_SP:
        if (crouching) return MV_RISE;
        if (fwd) return MV_RUSH;
        return shotOut ? MV_NONE : MV_FIREBALL;
    case BTN_SUPER: return meter >= SUPER_COST ? MV_BEAM : MV_NONE;
    default: return MV_NONE;
    }
}

// ---------------------------------------------------------------------------
void Fighter::reset(CharacterDef* c, int idx, float startX, int face)
{
    ch = c; index = idx;
    x = startX; y = 0; vx = vy = kbx = 0;
    facing = face;
    state = FS_INTRO; stateFrame = 0;
    move = MV_NONE; moveFrame = 0; moveConnected = moveHasHit = airAttackUsed = false;
    jumpDir = 0;
    maxHealth = c ? c->health : 1000; health = maxHealth;
    stunLeft = 0; stunTotal = 1; blockCrouch = false; bounced = false;
    comboTaken = comboDamage = juggle = 0;
    shotOut = dangerNear = false; landFrame = -1;
    buffered = BTN_NONE; bufferTime = 0;
    in = InputState{};
    events = 0;
    animTime = idx * 23.0f;
    visualYaw = face > 0 ? 90.0f : -90.0f;
    flash = shake = 0;
    blendLeft = 0;
    pose = animEval(ANIM_INTRO, 0);
    if (facing < 0) pose = poseMirror(pose);
    fromPose = pose;
    hitActive = false;
    updatePose();
}

void Fighter::beginBlend(int frames)
{
    fromPose = pose;
    blendLeft = blendTotal = std::max(frames, 0);
    if (blendTotal < 1) blendTotal = 1;
}

void Fighter::setState(FighterState s, int blend)
{
    state = s;
    stateFrame = 0;
    beginBlend(blend);
}

void Fighter::startAttack(MoveId m)
{
    move = m;
    moveFrame = 0;
    moveConnected = moveHasHit = false;
    landFrame = -1;
    buffered = BTN_NONE; bufferTime = 0;
    attackSerial++;
    const MoveDef& d = moveDef(m);
    if (m == MV_BEAM) { meter = 0.0f; events |= EV_SUPER; }
    else if (d.special) events |= EV_SPECIAL | EV_WHOOSH_HEAVY;
    else events |= d.heavy ? EV_WHOOSH_HEAVY : EV_WHOOSH_LIGHT;
    setState(FS_ATTACK, 2);
}

bool Fighter::tryGroundAttack(const InputState& input)
{
    if (buffered == BTN_NONE) return false;
    MoveId m = pickGroundMove(buffered, input);
    if (m == MV_NONE) { buffered = BTN_NONE; bufferTime = 0; return false; }
    startAttack(m);
    return true;
}

void Fighter::startWin() { vx = kbx = 0; move = MV_NONE; setState(FS_WIN, 8); }

void Fighter::forceKO()
{
    health = 0; move = MV_NONE;
    if (grounded()) { vx = -facing * 1.6f; vy = 3.8f; y = 0.01f; bounced = false; setState(FS_FALL, 2); }
}

bool Fighter::beamActive() const
{
    if (state != FS_ATTACK || move != MV_BEAM) return false;
    const MoveDef& m = MOVES[MV_BEAM];
    return moveFrame >= m.startup && moveFrame < m.startup + m.active;
}

bool Fighter::invulnerable() const
{
    if (state == FS_DOWN || state == FS_GETUP || state == FS_KO || state == FS_WIN || state == FS_INTRO) return true;
    if (state == FS_ATTACK && move == MV_RISE && moveFrame < 9) return true;      // rising upper beats everything on the way up
    if (state == FS_ATTACK && move == MV_BEAM && moveFrame < MOVES[MV_BEAM].startup) return true;
    return false;
}

bool Fighter::threatens(const Fighter& other) const
{
    if (!attacking() || moveHasHit) return false;
    if (move == MV_FIREBALL) return false;
    if (move == MV_BEAM) return moveFrame < MOVES[MV_BEAM].startup + MOVES[MV_BEAM].active;
    const MoveDef& m = moveDef(move);
    if (moveFrame >= m.startup + m.active + 1) return false;
    return fabsf(x - other.x) < (move == MV_RUSH ? 3.4f : 2.3f);
}

void Fighter::bufferInput(const InputState& input)
{
    Button pressed = input.super ? BTN_SUPER : input.sp ? BTN_SP : input.hk ? BTN_HK : input.hp ? BTN_HP : input.lk ? BTN_LK : input.lp ? BTN_LP : BTN_NONE;
    if (pressed != BTN_NONE) { buffered = pressed; bufferTime = 10; }
}

// ---------------------------------------------------------------------------
// One logic tick
// ---------------------------------------------------------------------------
void Fighter::update(const InputState& input, Fighter& opp)
{
    in = input;
    stateFrame++;
    animTime += 1.0f;
    bufferInput(input);
    if (bufferTime > 0 && --bufferTime == 0) buffered = BTN_NONE;

    int dir = (opp.x >= x) ? 1 : -1;
    bool fwd = facing > 0 ? input.right : input.left;
    bool back = facing > 0 ? input.left : input.right;
    float spd = ch ? ch->speed : 1.0f;

    switch (state)
    {
    case FS_IDLE: case FS_WALK_F: case FS_WALK_B: case FS_CROUCH:
    {
        comboTaken = comboDamage = juggle = 0;
        if (dir != facing && fabsf(opp.x - x) > 0.08f)
        {
            facing = dir;
            beginBlend(8);
            fwd = facing > 0 ? input.right : input.left;
            back = facing > 0 ? input.left : input.right;
        }
        if (tryGroundAttack(input)) break;
        if (input.up)
        {
            jumpDir = fwd ? 1 : (back ? -1 : 0);
            vx = 0;
            setState(FS_PREJUMP, 3);
            break;
        }
        if (back && !fwd && (opp.threatens(*this) || dangerNear))
        {
            blockCrouch = input.down; stunLeft = 0; vx = 0;
            setState(FS_BLOCK, 3);
            break;
        }
        FighterState want = input.down ? FS_CROUCH : (fwd && !back) ? FS_WALK_F : (back && !fwd) ? FS_WALK_B : FS_IDLE;
        vx = want == FS_WALK_F ? facing * WALK_FWD * spd : want == FS_WALK_B ? -facing * WALK_BACK * spd : 0.0f;
        if (want != state) setState(want, 6);
        break;
    }
    case FS_BLOCK:
        vx = 0;
        if (stunLeft > 0) { stunLeft--; break; }
        if (!(back && (opp.threatens(*this) || dangerNear))) setState(input.down ? FS_CROUCH : FS_IDLE, 5);
        else if (blockCrouch != input.down) { blockCrouch = input.down; beginBlend(4); }
        break;

    case FS_PREJUMP:
        if (stateFrame >= 4)
        {
            vy = JUMP_SPEED; vx = jumpDir * facing * JUMP_DRIFT; y = 0.001f;
            airAttackUsed = false; move = MV_NONE;
            events |= EV_JUMP;
            setState(FS_JUMP, 3);
        }
        break;

    case FS_JUMP:
        vy -= GRAVITY * TICK_DT;
        y += vy * TICK_DT;
        if (move == MV_NONE && !airAttackUsed && buffered != BTN_NONE && buffered != BTN_SUPER)
        {
            if (buffered == BTN_SP)
            {
                move = MV_DIVE;
                vx = facing * 5.6f; vy = std::min(vy, -5.5f);
                events |= EV_SPECIAL | EV_WHOOSH_HEAVY;
            }
            else
            {
                move = (buffered == BTN_LP || buffered == BTN_HP) ? MV_JP : MV_JK;
                events |= EV_WHOOSH_LIGHT;
            }
            moveFrame = 0; moveConnected = moveHasHit = false; airAttackUsed = true;
            buffered = BTN_NONE; bufferTime = 0;
            attackSerial++;
            beginBlend(2);
        }
        else if (move != MV_NONE) moveFrame++;
        if (y <= 0.0f && vy < 0.0f)
        {
            bool dived = (move == MV_DIVE);
            y = 0; vy = 0; vx = 0; move = MV_NONE;
            facing = dir;
            events |= EV_LAND;
            setState(FS_LAND, 3);
            if (dived) stateFrame = -6;      // extra landing recovery after a dive kick
        }
        break;

    case FS_LAND:
        vx = 0;
        if (buffered != BTN_NONE && stateFrame >= 2 && tryGroundAttack(input)) break;
        if (stateFrame >= 4) setState(input.down ? FS_CROUCH : FS_IDLE, 5);
        break;

    case FS_ATTACK:
    {
        const MoveDef& m = moveDef(move);
        moveFrame++;
        if (move == MV_RUSH) vx = (moveFrame >= 5 && moveFrame <= 15) ? facing * 7.2f : vx * 0.72f;
        else vx = (moveFrame <= m.startup) ? facing * m.lunge : vx * 0.7f;

        if (move == MV_FIREBALL && moveFrame == m.startup) events |= EV_SHOT;
        if (move == MV_BEAM && moveFrame == m.startup) events |= EV_BEAM;
        if (move == MV_RISE)
        {
            if (moveFrame == m.startup) { vy = 7.0f; y = 0.01f; vx = facing * 1.5f; events |= EV_JUMP; }
            if (y > 0.0f)
            {
                vx = facing * 1.5f;
                vy -= GRAVITY * TICK_DT;
                y += vy * TICK_DT;
                if (y <= 0.0f) { y = 0; vy = 0; vx = 0; landFrame = moveFrame; events |= EV_LAND; }
            }
        }

        // cancel into a stronger move when the attack connected
        if (moveConnected && buffered != BTN_NONE && y <= 0.0f && move != MV_BEAM &&
            moveFrame >= m.startup && moveFrame <= m.startup + m.active + 10)
        {
            MoveId next = pickGroundMove(buffered, input);
            if (next != MV_NONE && moveDef(next).rank > m.rank) { startAttack(next); break; }
        }
        bool finished = moveFrame >= m.total;
        if (move == MV_RISE) finished = (landFrame >= 0 && moveFrame >= landFrame + 14 && moveFrame >= 30) || moveFrame > 120;
        if (finished)
        {
            bool crouchEnd = isCrouchMove(move) && input.down;
            move = MV_NONE; vx = 0;
            setState(input.down ? FS_CROUCH : FS_IDLE, crouchEnd ? 2 : 5);
        }
        break;
    }
    case FS_HIT:
        vx = 0;
        if (--stunLeft <= 0) setState(input.down ? FS_CROUCH : FS_IDLE, 5);
        break;

    case FS_FALL:
        vy -= GRAVITY * TICK_DT;
        y += vy * TICK_DT;
        if (y <= 0.0f && vy < 0.0f)
        {
            y = 0;
            events |= EV_BODY_FALL;
            if (!bounced && vy < -5.5f) { bounced = true; vy = -vy * 0.30f; vx *= 0.55f; y = 0.001f; }
            else { kbx = vx * 0.6f; vx = 0; vy = 0; setState(health <= 0 ? FS_KO : FS_DOWN, 4); }
        }
        break;

    case FS_DOWN:
        vx = 0;
        if (stateFrame >= 26) { facing = dir; setState(FS_GETUP, 0); }
        break;
    case FS_GETUP:
        vx = 0;
        if (stateFrame >= 32) setState(FS_IDLE, 4);
        break;
    case FS_INTRO: case FS_KO: case FS_WIN:
        vx = 0;
        break;
    }

    x += (vx + kbx) * TICK_DT;
    kbx *= 0.86f;
    if (fabsf(kbx) < 0.02f) kbx = 0;
}

// ---------------------------------------------------------------------------
// Taking a hit
// ---------------------------------------------------------------------------
HitResult Fighter::receiveHit(const MoveDef& m, Fighter& att, Vector2 at)
{
    int toward = (att.x >= x) ? 1 : -1;
    bool back = toward > 0 ? in.left : in.right;
    bool air = airborne();
    bool canBlock = !air && ((isFree() && back) || state == FS_BLOCK);
    if (canBlock && m.level == HL_LOW && !in.down) canBlock = false;
    if (canBlock && m.level == HL_OVERHEAD && in.down) canBlock = false;
    facing = toward;
    float power = att.ch ? att.ch->power : 1.0f;

    if (canBlock)
    {
        blockCrouch = in.down;
        stunLeft = stunTotal = m.blockstun;
        kbx = -toward * m.pushBlock; vx = 0;
        if (m.chip > 0.0f) health = std::max(1, health - (int)roundf(m.damage * power * m.chip));
        if (state != FS_BLOCK) setState(FS_BLOCK, 2); else stateFrame = 0;
        return HIT_BLOCKED;
    }

    float scale = m.noScale ? 1.0f : std::max(0.35f, 1.0f - 0.12f * comboTaken);
    int dmg = (int)roundf(m.damage * power * scale);
    health = std::max(0, health - dmg);
    comboTaken++;
    comboDamage += dmg;
    flash = 1.0f;
    bool wasCrouch = state == FS_CROUCH || (state == FS_ATTACK && isCrouchMove(move)) ||
                     (state == FS_HIT && hitAnim == ANIM_HIT_CROUCH) || (state == FS_BLOCK && blockCrouch);
    bool wasFalling = state == FS_FALL;
    move = MV_NONE;

    if (health <= 0 || m.knockdown || air)
    {
        float lvx = m.launchVx, lvy = m.launchVy;
        if (health <= 0) { lvx = std::max(lvx, 3.6f); lvy = std::max(lvy, 4.8f); }
        if (wasFalling) { if (!m.noScale) juggle++; lvy *= 0.85f; }
        vx = -toward * lvx; vy = lvy; kbx = 0;
        if (y <= 0.0f) y = 0.01f;
        bounced = false;
        setState(FS_FALL, 2);
    }
    else
    {
        stunLeft = stunTotal = m.hitstun;
        hitAnim = wasCrouch ? ANIM_HIT_CROUCH : ((at.y - y) > 1.02f ? ANIM_HIT_HIGH : ANIM_HIT_LOW);
        kbx = -toward * m.pushHit; vx = 0;
        setState(FS_HIT, 1);
    }
    return HIT_CONNECT;
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------
void Fighter::updatePose()
{
    Pose target;
    switch (state)
    {
    case FS_INTRO:   target = animEval(ANIM_INTRO, (float)stateFrame); break;
    case FS_IDLE:    target = animEval(ANIM_IDLE, animTime); break;
    case FS_WALK_F:  target = animEval(ANIM_WALK_F, (float)stateFrame); break;
    case FS_WALK_B:  target = animEval(ANIM_WALK_B, (float)stateFrame); break;
    case FS_CROUCH:  target = animEval(ANIM_CROUCH, 6.0f); break;
    case FS_PREJUMP: target = animEval(ANIM_CROUCH, 3.4f); break;
    case FS_LAND:    target = animEval(ANIM_CROUCH, 3.0f); break;
    case FS_JUMP:
        if (move != MV_NONE) target = animEval(moveDef(move).anim, (float)moveFrame);
        else target = animEval(ANIM_JUMP, 40.0f * stateFrame / (2.0f * JUMP_SPEED / GRAVITY * TICK_RATE));
        break;
    case FS_ATTACK:
        if (move == MV_RISE && landFrame >= 0) target = animEval(ANIM_RISE, 42.0f + (float)(moveFrame - landFrame));
        else if (move == MV_RISE) target = animEval(ANIM_RISE, std::min((float)moveFrame, 38.0f));
        else target = animEval(moveDef(move).anim, (float)moveFrame);
        break;
    case FS_BLOCK:
    {
        target = animEval(blockCrouch ? ANIM_BLOCK_CROUCH : ANIM_BLOCK, 0);
        float k = stunTotal > 0 ? (float)stunLeft / (float)stunTotal : 0.0f;
        target.hipsPos.z -= 0.07f * k;
        target.chestRot.x -= 7.0f * k;
        target.headRot.x += 5.0f * k;
        break;
    }
    case FS_HIT:     target = animEval(hitAnim, 30.0f * (1.0f - (float)stunLeft / (float)std::max(stunTotal, 1))); break;
    case FS_FALL:    target = animEval(ANIM_FALL, (float)stateFrame + (bounced ? 14.0f : 0.0f)); break;
    case FS_DOWN: case FS_KO: target = animEval(ANIM_DOWN, 0); break;
    case FS_GETUP:   target = animEval(ANIM_GETUP, (float)stateFrame); break;
    case FS_WIN:     target = animEval(ANIM_WIN, (float)stateFrame); break;
    }
    if (facing < 0) target = poseMirror(target);

    if (blendLeft > 0)
    {
        float t = 1.0f - (float)(blendLeft - 1) / (float)blendTotal;
        pose = poseLerp(fromPose, target, smooth01(t));
        blendLeft--;
    }
    else pose = target;

    float yawTarget = facing > 0 ? 90.0f : -90.0f;
    visualYaw += clampf(yawTarget - visualYaw, -30.0f, 30.0f);
    if (flash > 0.0f) flash = std::max(0.0f, flash - 0.10f);

    if (ch && ch->rig.valid)
    {
        ch->rig.solve(pose, bones);
        ch->rig.skinMatrices(bones, skin);
        computeBoxes();
    }
}

Matrix Fighter::worldMatrix() const
{
    float z = index == 0 ? 0.06f : -0.06f;
    return MatrixMultiply(MatrixRotateY(visualYaw * DEG2RAD), MatrixTranslate(x + shake, y, z));
}

Vector3 Fighter::bonePosWorld(int bone) const
{
    if (bone < 0 || bone >= (int)bones.size()) return v3(x, y + 1.0f, 0);
    return Vector3Transform(bones[bone].pos, worldMatrix());
}

Vector3 Fighter::chestWorld() const
{
    if (!ch || !ch->rig.valid) return v3(x, y + 1.1f, 0);
    return bonePosWorld(ch->rig.spine[2]);
}

Vector3 Fighter::handsWorld() const
{
    if (!ch || !ch->rig.valid) return v3(x + facing * 0.8f, y + 1.15f, 0);
    Vector3 a = bonePosWorld(ch->rig.hand[0]), b = bonePosWorld(ch->rig.hand[1]);
    Vector3 m = Vector3Scale(a + b, 0.5f);
    return m;
}

// which body side carries the hit box of the current move (poses are mirrored when facing left)
int Fighter::hitSide() const
{
    if (move == MV_NONE) return LEFT;
    Limb l = moveDef(move).limb;
    int side = (l == LIMB_LEAD_HAND || l == LIMB_LEAD_FOOT) ? LEFT : RIGHT;
    return facing < 0 ? 1 - side : side;
}

Vector3 Fighter::limbTipWorld() const
{
    if (!ch || !ch->rig.valid || move == MV_NONE) return chestWorld();
    const Rig& r = ch->rig;
    Limb l = moveDef(move).limb;
    int side = hitSide();
    if (l == LIMB_LEAD_HAND || l == LIMB_REAR_HAND)
    {
        Vector3 e = bonePosWorld(r.forearm[side]), w = bonePosWorld(r.hand[side]);
        return w + Vector3Scale(Vector3Normalize(w - e), 0.10f);
    }
    return bonePosWorld(r.toe[side] >= 0 ? r.toe[side] : r.foot[side]);
}

// ---------------------------------------------------------------------------
// Hit / hurt boxes, derived from the animated skeleton
// ---------------------------------------------------------------------------
void Fighter::computeBoxes()
{
    const Rig& r = ch->rig;
    auto P = [&](int bone) { const Vector3& p = bones[bone].pos; return Vector2{ x + facing * p.z, y + p.y }; };
    auto grow = [](Box& b, Vector2 p) { b.x0 = std::min(b.x0, p.x); b.x1 = std::max(b.x1, p.x); b.y0 = std::min(b.y0, p.y); b.y1 = std::max(b.y1, p.y); };
    auto boxAt = [](Vector2 p) { return Box{ p.x, p.y, p.x, p.y }; };
    auto pad = [](Box& b, float px, float py) { b.x0 -= px; b.x1 += px; b.y0 -= py; b.y1 += py; };

    hurt.clear();
    bool noHurt = invulnerable() || (state == FS_FALL && juggle >= 2);
    if (!noHurt)
    {
        Vector2 h = P(r.head);
        Vector3 up = rotate(bones[r.head].rot, v3(0, 0.10f, 0));
        h.x += facing * up.z; h.y += up.y;
        Box head = boxAt(h); pad(head, 0.12f, 0.13f);
        Box torso = boxAt(P(r.hips));
        grow(torso, P(r.spine[2])); grow(torso, P(r.neck)); grow(torso, P(r.arm[0])); grow(torso, P(r.arm[1]));
        pad(torso, 0.13f, 0.07f);
        Box legs = boxAt(P(r.upleg[0]));
        for (int s = 0; s < 2; s++) { grow(legs, P(r.upleg[s])); grow(legs, P(r.leg[s])); grow(legs, P(r.foot[s])); }
        pad(legs, 0.09f, 0.09f);
        legs.y0 = std::max(legs.y0, y - 0.02f);
        hurt.push_back(head); hurt.push_back(torso); hurt.push_back(legs);
    }

    hitActive = false;
    if (attacking() && !moveHasHit)
    {
        const MoveDef& m = moveDef(move);
        if (m.radius > 0.0f && moveFrame >= m.startup && moveFrame < m.startup + m.active)
        {
            int side = hitSide();
            Box b;
            if (m.limb == LIMB_LEAD_HAND || m.limb == LIMB_REAR_HAND)
            {
                Vector2 e = P(r.forearm[side]), w = P(r.hand[side]);
                Vector2 d = Vector2Normalize(Vector2Subtract(w, e));
                b = boxAt(Vector2Lerp(e, w, 0.4f));
                grow(b, Vector2Add(w, Vector2Scale(d, 0.15f)));
            }
            else
            {
                Vector2 k = P(r.leg[side]), a = P(r.foot[side]);
                b = boxAt(Vector2Lerp(k, a, 0.45f));
                grow(b, a);
                if (r.toe[side] >= 0)
                {
                    Vector2 t = P(r.toe[side]);
                    grow(b, Vector2Add(t, Vector2Scale(Vector2Subtract(t, a), 0.5f)));
                }
            }
            pad(b, m.radius, m.radius);
            hit = b;
            hitActive = true;
        }
    }
}
