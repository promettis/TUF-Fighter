// ============================================================================
//  TUF FIGHTER - game.cpp
// ============================================================================
#include "game.h"
#include "audio.h"

// ---------------------------------------------------------------------------
// Stage placement: scale the model to building size, sit it on the ground
// and move it behind the fighting court.
// ---------------------------------------------------------------------------
void StageDef::place()
{
    float w = data.bmax.x - data.bmin.x;
    float d = data.bmax.z - data.bmin.z;
    float s = 46.0f / std::max(std::max(w, d), 0.001f);
    float cx = (data.bmin.x + data.bmax.x) * 0.5f;
    const float frontZ = -27.0f;                       // distance of the facade behind the fighters
    world = MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(-cx * s, -data.bmin.y * s - 0.04f, frontZ - data.bmax.z * s));
}

// ---------------------------------------------------------------------------
// CPU player
// ---------------------------------------------------------------------------
void CpuBrain::reset(int diff)
{
    difficulty = diff; timer = 20; action = 0; plan.clear();
    lastSerial = -1; willBlock = false; oppWasAir = false; wantAntiAir = false;
    shotSeen = -1; shotReaction = 0;
}

static Button buttonFor(MoveId m)
{
    switch (m)
    {
    case MV_LP: case MV_CLP: case MV_JP: return BTN_LP;
    case MV_HP: case MV_CHP: case MV_FHP: return BTN_HP;
    case MV_LK: case MV_CLK: case MV_JK: return BTN_LK;
    case MV_FIREBALL: case MV_RUSH: case MV_RISE: case MV_DIVE: return BTN_SP;
    case MV_BEAM: return BTN_SUPER;
    default: return BTN_HK;
    }
}
static bool needsCrouch(MoveId m) { return m == MV_CLP || m == MV_CHP || m == MV_CLK || m == MV_CHK || m == MV_RISE; }
static bool needsForward(MoveId m) { return m == MV_FHP || m == MV_FHK || m == MV_RUSH; }
static void press(InputState& o, Button b)
{
    switch (b)
    {
    case BTN_LP: o.lp = true; break; case BTN_HP: o.hp = true; break;
    case BTN_LK: o.lk = true; break; case BTN_HK: o.hk = true; break;
    case BTN_SP: o.sp = true; break; case BTN_SUPER: o.super = true; break;
    default: break;
    }
}

InputState CpuBrain::think(const Fighter& me, const Fighter& opp, Rng& rng, float incomingShot, int shotId)
{
    static const float BLOCK[3] = { 0.20f, 0.50f, 0.82f };
    static const float AGGRO[3] = { 0.40f, 0.62f, 0.82f };
    static const float COMBO[3] = { 0.15f, 0.55f, 0.90f };
    static const float ANTIAIR[3] = { 0.10f, 0.40f, 0.78f };
    static const float PACE[3] = { 1.7f, 1.0f, 0.65f };
    static const float SPECIAL[3] = { 0.35f, 0.75f, 1.0f };

    InputState o;
    float dist = fabsf(opp.x - me.x);
    int toward = opp.x >= me.x ? 1 : -1;
    auto holdFwd = [&]() { (toward > 0 ? o.right : o.left) = true; };
    auto holdBack = [&]() { (toward > 0 ? o.left : o.right) = true; };
    auto doMove = [&](MoveId m) {
        o.down = needsCrouch(m);
        if (needsForward(m)) holdFwd();
        press(o, buttonFor(m));
    };

    // --- defence: decide once per incoming attack whether we block it ---
    if (opp.attacking() && opp.attackSerial != lastSerial)
    {
        lastSerial = opp.attackSerial;
        willBlock = rng.chance(BLOCK[difficulty]);
    }
    if (opp.threatens(me) && willBlock && (me.isFree() || me.state == FS_BLOCK) && plan.empty())
    {
        holdBack();
        HitLevel lv = moveDef(opp.move).level;
        o.down = (lv == HL_LOW);
        return o;
    }
    // --- incoming projectile: block it, jump over it, or eat it ---
    if (incomingShot >= 0.0f)
    {
        if (shotId != shotSeen)
        {
            shotSeen = shotId;
            float r = rng.unit();
            shotReaction = r < BLOCK[difficulty] * 0.75f ? 1 : (r < BLOCK[difficulty] * 0.75f + 0.25f ? 2 : 0);
        }
        if (me.isFree() || me.state == FS_BLOCK)
        {
            if (shotReaction == 1 && incomingShot < 2.2f) { holdBack(); return o; }
            if (shotReaction == 2 && incomingShot < 1.9f && incomingShot > 1.2f && me.isFree()) { o.up = true; holdFwd(); return o; }
        }
    }
    if (me.state == FS_BLOCK) { holdBack(); return o; }

    // --- anti air ---
    bool oppAir = opp.state == FS_JUMP;
    if (oppAir && !oppWasAir) wantAntiAir = rng.chance(ANTIAIR[difficulty]);
    oppWasAir = oppAir;
    if (oppAir && wantAntiAir && me.isFree() && dist < 1.45f && opp.vy < 1.5f && opp.y < 1.9f && opp.y > 0.5f)
    {
        doMove(rng.chance(0.5f) ? MV_RISE : MV_CHP);
        wantAntiAir = false;
        return o;
    }

    // --- super: fire it when it is likely to connect ---
    if (me.meter >= SUPER_COST && me.isFree() && plan.empty() && dist < 5.0f && !opp.airborne() && !opp.invulnerable())
    {
        bool punish = (opp.state == FS_ATTACK && opp.moveFrame > 4) || opp.state == FS_HIT;
        if (punish ? rng.chance(0.25f * SPECIAL[difficulty]) : rng.chance(0.006f * SPECIAL[difficulty])) { doMove(MV_BEAM); return o; }
    }

    // --- carry out a planned attack chain ---
    if (!plan.empty())
    {
        MoveId next = plan.front();
        if (me.attacking() && me.move == next) { plan.erase(plan.begin()); return o; }
        if (me.isFree())
        {
            float reach = (next == MV_FIREBALL) ? 99.0f : (next == MV_RUSH ? 3.0f : 1.6f);
            if (dist > reach) plan.clear();
            else { doMove(next); return o; }
        }
        else if (me.state == FS_ATTACK)
        {
            const MoveDef& m = moveDef(me.move);
            if (me.moveConnected) { doMove(next); return o; }
            if (me.moveFrame > m.startup + m.active + 1) plan.clear();      // whiffed
            return o;
        }
        else plan.clear();
    }
    if (!me.isFree()) return o;

    // --- movement / new decisions ---
    if (--timer <= 0)
    {
        float r = rng.unit();
        float ag = AGGRO[difficulty];
        float pace = PACE[difficulty];
        float sp = SPECIAL[difficulty];
        if (dist > 2.6f)
        {
            if (!me.shotOut && r < 0.24f * sp) { plan = { MV_FIREBALL }; action = 0; timer = 44; }
            else if (r < 0.24f * sp + ag * 0.70f) { action = 1; timer = (int)(rng.range(18, 40) * pace); }
            else if (r < 0.24f * sp + ag * 0.70f + 0.08f) { action = 6; timer = 6; }
            else { action = 0; timer = (int)(rng.range(14, 34) * pace); }
        }
        else if (dist > 1.22f)
        {
            if (r < ag * 0.50f) { action = 1; timer = (int)(rng.range(6, 16)); }
            else if (r < ag * 0.50f + 0.13f && dist < 1.62f) { plan = { MV_HK }; action = 0; timer = 30; }
            else if (r < ag * 0.50f + 0.21f && dist < 1.45f) { plan = { MV_LK }; action = 0; timer = 20; }
            else if (r < ag * 0.50f + 0.21f + 0.10f * sp) { plan = { MV_RUSH }; action = 0; timer = 40; }
            else if (r < ag * 0.50f + 0.21f + 0.16f * sp && dist < 1.8f) { plan = { MV_FHK }; action = 0; timer = 34; }
            else if (r < ag * 0.50f + 0.45f) { action = 6; timer = 6; }
            else if (r < ag * 0.50f + 0.56f) { action = 2; timer = (int)(rng.range(10, 22) * pace); }
            else { action = 0; timer = (int)(rng.range(10, 24) * pace); }
        }
        else
        {
            if (r < ag)
            {
                bool full = rng.chance(COMBO[difficulty]);
                bool ender = full && rng.chance(0.5f * sp);
                switch (rng.pick(10))
                {
                case 0: plan = full ? std::vector<MoveId>{ MV_LP, MV_HP } : std::vector<MoveId>{ MV_LP }; break;
                case 1: plan = full ? std::vector<MoveId>{ MV_LP, MV_LK, MV_HP } : std::vector<MoveId>{ MV_LP, MV_LK }; break;
                case 2: plan = full ? std::vector<MoveId>{ MV_LK, MV_HK } : std::vector<MoveId>{ MV_LK }; break;
                case 3: plan = full ? std::vector<MoveId>{ MV_CLK, MV_CHK } : std::vector<MoveId>{ MV_CLK }; break;
                case 4: plan = { MV_HP }; break;
                case 5: plan = full ? std::vector<MoveId>{ MV_CLP, MV_CHP } : std::vector<MoveId>{ MV_CLP }; break;
                case 6: plan = full ? std::vector<MoveId>{ MV_LP, MV_HP, MV_HK } : std::vector<MoveId>{ MV_HK }; break;
                case 7: plan = { MV_FHP }; break;
                case 8: plan = full ? std::vector<MoveId>{ MV_LK, MV_HP } : std::vector<MoveId>{ MV_FHK }; break;
                default: plan = { MV_CHK }; break;
                }
                // finish some chains with a special move
                if (ender && !plan.empty() && plan.back() != MV_HK && plan.back() != MV_CHK && plan.back() != MV_CHP)
                    plan.push_back(rng.chance(0.5f) ? MV_RUSH : (me.shotOut ? MV_RISE : MV_FIREBALL));
                action = 0; timer = (int)(rng.range(16, 30) * pace);
            }
            else if (r < ag + (1.0f - ag) * 0.5f) { action = 2; timer = (int)(rng.range(10, 24)); }
            else { action = 3; timer = (int)(rng.range(12, 26) * pace); }
        }
    }
    switch (action)
    {
    case 1: holdFwd(); break;
    case 2: holdBack(); break;
    case 3: o.down = true; holdBack(); break;
    case 6: o.up = true; holdFwd(); action = 1; break;
    default: break;
    }
    return o;
}

// ---------------------------------------------------------------------------
// Match flow
// ---------------------------------------------------------------------------
void Match::start(CharacterDef* a, CharacterDef* b, bool cpuA, bool cpuB, int diff, StageDef* st)
{
    stage = st;
    cpu[0] = cpuA; cpu[1] = cpuB;
    difficulty = diff;
    wins[0] = wins[1] = 0;
    round = 1;
    matchWinner = -1;
    rng.s = (uint32_t)(GetTime() * 1000.0) | 1u;
    f[0].ch = a; f[1].ch = b;
    f[0].meter = f[1].meter = 0.0f;
    startRound();
}

void Match::startRound()
{
    f[0].reset(f[0].ch, 0, -1.55f, 1);
    f[1].reset(f[1].ch, 1, 1.55f, -1);
    brain[0].reset(difficulty); brain[1].reset(difficulty);
    timeLeft = 99.0f;
    phase = MP_INTRO; phaseFrame = 0;
    roundWinner = -1; timeUp = false;
    hitstop = 0; hitVictim = -1;
    for (int i = 0; i < 2; i++)
    {
        shownHealth[i] = ghostHealth[i] = 1.0f; ghostDelay[i] = 0;
        comboCount[i] = comboDamage[i] = 0; comboTimer[i] = 0; comboPop[i] = 0;
        trail[i].clear();
        beam[i] = BeamVisual{};
    }
    parts.clear();
    shots.clear();
    superFreeze = 0; superOwner = -1; superBanner = 99.0f;
    annTime = annDur = 0;
    camSnap = true;
    koFlash = 0; whiteFlash = 0; fovKick = 0; dim = 1.0f;
    musicDuck(0.0f);
}

void Match::announce(const std::string& text, const std::string& sub, float dur, Color col)
{
    annText = text; annSub = sub; annTime = 0; annDur = dur; annColor = col;
}

void Match::addMeter(int attacker, int dmg, bool blocked)
{
    Fighter& a = f[attacker]; Fighter& d = f[1 - attacker];
    a.meter = std::min(SUPER_COST, a.meter + (blocked ? 2.5f : dmg * 0.11f));
    d.meter = std::min(SUPER_COST, d.meter + (blocked ? 2.0f : dmg * 0.07f));
}

void Match::tick(const InputState in[2])
{
    phaseFrame++;

    // ---- round flow ----
    if (phase == MP_INTRO)
    {
        if (phaseFrame == 24)
        {
            bool final = (wins[0] == roundsToWin - 1 && wins[1] == roundsToWin - 1);
            announce(final ? "FINAL ROUND" : TextFormat("ROUND %d", round), "", 1.25f, Color{ 255, 255, 255, 255 });
            sfxPlay(SFX_ROUND, 0.8f);
        }
        if (phaseFrame == 104) { announce("FIGHT!", "", 0.85f, Color{ 255, 196, 40, 255 }); sfxPlay(SFX_FIGHT); }
        if (phaseFrame >= 112)
        {
            phase = MP_FIGHT; phaseFrame = 0;
            for (Fighter& p : f) p.setState(FS_IDLE, 6);
        }
    }
    // slow motion right after the knock-out blow
    if (phase == MP_KO && !timeUp && phaseFrame < 84 && (phaseFrame % 3) != 0) return;

    // ---- super freeze: only the fighter performing the super keeps moving ----
    if (superFreeze > 0)
    {
        superFreeze--;
        Fighter& o = f[superOwner];
        o.moveFrame++; o.stateFrame++;
        o.updatePose();
        Vector3 h = o.handsWorld();
        spawnCharge(h, o.ch->fxA, o.ch->fxB, 1.5f, 3);
        camShake = std::max(camShake, 0.012f);
        return;
    }

    // what each fighter knows about incoming projectiles / beams
    float incoming[2] = { -1.0f, -1.0f };
    int incomingId[2] = { -1, -1 };
    for (int i = 0; i < 2; i++)
    {
        f[i].shotOut = false;
        f[i].dangerNear = f[1 - i].beamActive();
    }
    for (const Projectile& s : shots)
    {
        if (s.dead) continue;
        f[s.owner].shotOut = true;
        int v = 1 - s.owner;
        float d = (f[v].x - s.x) * (s.vx > 0 ? 1.0f : -1.0f);       // distance still to travel
        if (d > -0.3f && (incoming[v] < 0 || d < incoming[v])) { incoming[v] = d; incomingId[v] = s.id; }
        if (d > -0.3f && d < 1.9f) f[v].dangerNear = true;
    }

    InputState use[2];
    for (int i = 0; i < 2; i++)
    {
        if (phase != MP_FIGHT) use[i] = InputState{};
        else if (cpu[i]) use[i] = brain[i].think(f[i], f[1 - i], rng, incoming[i], incomingId[i]);
        else use[i] = in[i];
    }

    if (hitstop > 0)
    {
        hitstop--;
        for (int i = 0; i < 2; i++) { f[i].bufferInput(use[i]); f[i].in = use[i]; }
        if (hitVictim >= 0) f[hitVictim].shake = ((hitstop & 1) ? 0.022f : -0.022f) * std::min(1.0f, hitstop / 4.0f);
        return;
    }
    f[0].shake = f[1].shake = 0;
    f[0].events = f[1].events = 0;

    f[0].update(use[0], f[1]);
    f[1].update(use[1], f[0]);
    resolvePush();
    f[0].updatePose();
    f[1].updatePose();
    if (phase == MP_FIGHT) resolveHits();
    handleEvents();
    updateShots();
    for (int i = 0; i < 2; i++) updateBeam(i);

    // ---- attack trails + charge effects ----
    for (int i = 0; i < 2; i++)
    {
        Fighter& p = f[i];
        if (p.attacking())
        {
            const MoveDef& m = moveDef(p.move);
            if (m.radius > 0.0f && p.moveFrame >= m.startup - 4 && p.moveFrame <= m.startup + m.active + 1)
            {
                trail[i].push_back({ p.limbTipWorld(), 0.0f });
                trailColor[i] = m.special ? p.ch->fxA : (m.heavy ? Color{ 255, 170, 70, 255 } : Color{ 200, 225, 255, 255 });
            }
            if (p.move == MV_FIREBALL && p.moveFrame >= 3 && p.moveFrame < m.startup)
                spawnCharge(p.handsWorld(), p.ch->fxA, p.ch->fxB, 0.7f, 2);
            if ((p.move == MV_RUSH || p.move == MV_RISE || p.move == MV_DIVE) && p.moveFrame < m.startup + m.active && (p.moveFrame % 2) == 0)
            {
                Particle g; g.kind = 4; g.pos = p.limbTipWorld() + v3(rng.range(-0.1f, 0.1f), rng.range(-0.1f, 0.1f), 0.1f);
                g.vel = v3(-p.facing * rng.range(0.5f, 2.0f), rng.range(-0.5f, 0.8f), 0);
                g.maxLife = g.life = rng.range(0.18f, 0.34f); g.size = rng.range(0.10f, 0.22f); g.col = rng.chance(0.5f) ? p.ch->fxA : p.ch->fxB;
                parts.push_back(g);
            }
        }
    }

    // ---- combo counters ----
    for (int i = 0; i < 2; i++)
    {
        const Fighter& victim = f[1 - i];
        if (victim.comboTaken >= 2 && victim.comboTaken != comboCount[i])
        {
            comboCount[i] = victim.comboTaken; comboDamage[i] = victim.comboDamage;
            comboTimer[i] = 1.7f; comboPop[i] = 1.0f;
        }
        if (victim.comboTaken == 0 && comboTimer[i] > 1.2f) comboTimer[i] = 1.2f;
    }

    if (phase == MP_FIGHT)
    {
        timeLeft -= TICK_DT;
        bool ko0 = f[0].health <= 0, ko1 = f[1].health <= 0;
        if (ko0 || ko1)
        {
            roundWinner = (ko0 && ko1) ? 2 : (ko0 ? 1 : 0);
            phase = MP_KO; phaseFrame = 0; timeUp = false;
            announce("K.O.", "", 2.0f, Color{ 255, 70, 50, 255 });
            sfxPlay(SFX_KO);
            musicDuck(0.75f);
            koFlash = 1.0f;
            camShake = std::max(camShake, 0.22f);
            hitstop = std::max(hitstop, 16);
            for (int i = 0; i < 2; i++)
                if (f[i].health <= 0) spawnBurst(f[i].chestWorld(), Color{ 255, 220, 150, 255 }, WHITE, 1.6f);
        }
        else if (timeLeft <= 0.0f)
        {
            timeLeft = 0;
            float h0 = (float)f[0].health / f[0].maxHealth, h1 = (float)f[1].health / f[1].maxHealth;
            roundWinner = fabsf(h0 - h1) < 0.0005f ? 2 : (h0 > h1 ? 0 : 1);
            phase = MP_KO; phaseFrame = 0; timeUp = true;
            announce("TIME UP", "", 1.8f, WHITE);
            sfxPlay(SFX_ROUND, 0.8f, 0.8f);
        }
    }
    else if (phase == MP_KO)
    {
        bool settled = true;
        for (const Fighter& p : f) if (p.airborne() || p.state == FS_ATTACK || p.state == FS_HIT) settled = false;
        if (phaseFrame > (timeUp ? 110 : 150) && settled)
        {
            phase = MP_WINPOSE; phaseFrame = 0;
            musicDuck(0.35f);
            shots.clear();
            if (roundWinner == 2) announce("DRAW", "", 2.4f, WHITE);
            else
            {
                wins[roundWinner]++;
                Fighter& w = f[roundWinner];
                bool perfect = w.health >= w.maxHealth;
                w.startWin();
                if (timeUp) f[1 - roundWinner].setState(FS_IDLE, 6);
                announce(w.ch->name + " WINS", perfect ? "PERFECT!" : "", 2.8f, Color{ 255, 214, 64, 255 });
                sfxPlay(SFX_WIN);
            }
        }
    }
    else if (phase == MP_WINPOSE)
    {
        if (phaseFrame > 215)
        {
            if (roundWinner != 2 && wins[roundWinner] >= roundsToWin) { matchWinner = roundWinner; phase = MP_OVER; phaseFrame = 0; }
            else { if (roundWinner != 2) round++; startRound(); }
        }
    }
}

void Match::resolvePush()
{
    Fighter& a = f[0]; Fighter& b = f[1];
    for (int iter = 0; iter < 2; iter++)
    {
        bool solid = !a.isDown() && !b.isDown() && fabsf(a.y - b.y) < 0.95f;
        float dx = b.x - a.x;
        float minD = PUSH_HALF * 2.0f;
        if (solid && fabsf(dx) < minD)
        {
            float dirn = dx > 0.0f ? 1.0f : (dx < 0.0f ? -1.0f : (float)a.facing);
            float pen = (minD - fabsf(dx)) * 0.5f;
            float k = (a.airborne() || b.airborne()) ? 0.35f : 1.0f;      // airborne fighters slide off instead of popping
            a.x -= dirn * pen * k; b.x += dirn * pen * k;
        }
        for (Fighter& p : f) p.x = clampf(p.x, -ARENA_HALF, ARENA_HALF);
    }
    float dx = b.x - a.x;
    if (fabsf(dx) > MAX_SEPARATION)
    {
        float mid = (a.x + b.x) * 0.5f, half = MAX_SEPARATION * 0.5f, sgn = dx > 0 ? 1.0f : -1.0f;
        a.x = mid - sgn * half; b.x = mid + sgn * half;
    }
}

void Match::resolveHits()
{
    struct Pending { bool hit = false; MoveId move = MV_NONE; Vector2 at{}; } pend[2];
    for (int a = 0; a < 2; a++)
    {
        Fighter& att = f[a]; Fighter& def = f[1 - a];
        if (!att.hitActive || att.moveHasHit) continue;
        for (const Box& hb : def.hurt)
        {
            if (!att.hit.overlaps(hb)) continue;
            pend[a].hit = true; pend[a].move = att.move;
            pend[a].at = Vector2{ (std::max(att.hit.x0, hb.x0) + std::min(att.hit.x1, hb.x1)) * 0.5f,
                                  (std::max(att.hit.y0, hb.y0) + std::min(att.hit.y1, hb.y1)) * 0.5f };
            break;
        }
    }
    for (int a = 0; a < 2; a++)
    {
        if (!pend[a].hit) continue;
        Fighter& att = f[a]; Fighter& def = f[1 - a];
        const MoveDef& m = moveDef(pend[a].move);
        int before = def.health;
        HitResult res = def.receiveHit(m, att, pend[a].at);
        att.moveHasHit = true;
        att.moveConnected = true;
        bool blocked = (res == HIT_BLOCKED);
        addMeter(a, before - def.health, blocked);
        hitstop = std::max(hitstop, blocked ? m.hitstop / 2 + 2 : m.hitstop);
        hitVictim = 1 - a;
        // a cornered victim pushes the attacker away instead
        if (fabsf(def.x) > ARENA_HALF - 0.10f && !att.airborne())
            att.kbx = -att.facing * (blocked ? m.pushBlock : m.pushHit) * 0.85f;
        Vector3 at = v3(pend[a].at.x, pend[a].at.y, 0.25f);
        spawnHit(at, m.heavy, blocked, att.facing, m.special ? att.ch->fxA : Color{ 255, 200, 90, 255 });
        if (m.special && !blocked) spawnBurst(at, att.ch->fxA, att.ch->fxB, 0.8f);
        float pan = clampf(at.x / ARENA_HALF, -1.0f, 1.0f) * 0.5f;
        float pitch = rng.range(0.93f, 1.07f);
        if (blocked) sfxPlay(SFX_BLOCK, 0.9f, pitch, pan);
        else sfxPlay(m.heavy ? SFX_HIT_HEAVY : SFX_HIT_LIGHT, 1.0f, pitch, pan);
        camShake = std::max(camShake, blocked ? 0.025f : (m.heavy ? 0.13f : 0.045f));
        if (m.heavy && !blocked) { fovKick = 1.0f; whiteFlash = std::max(whiteFlash, m.special ? 0.22f : 0.10f); }
    }
}

void Match::handleEvents()
{
    for (int i = 0; i < 2; i++)
    {
        Fighter& p = f[i];
        float pan = clampf(p.x / ARENA_HALF, -1.0f, 1.0f) * 0.5f;
        if (p.events & EV_WHOOSH_LIGHT) sfxPlay(SFX_WHOOSH_LIGHT, 0.8f, rng.range(0.95f, 1.1f), pan);
        if (p.events & EV_WHOOSH_HEAVY) sfxPlay(SFX_WHOOSH_HEAVY, 0.9f, rng.range(0.92f, 1.05f), pan);
        if (p.events & EV_JUMP) { sfxPlay(SFX_JUMP, 0.7f, 1.0f, pan); spawnDust(v3(p.x, 0.03f, 0.0f), 4); }
        if (p.events & EV_LAND) { sfxPlay(SFX_LAND, 0.8f, 1.0f, pan); spawnDust(v3(p.x, 0.03f, 0.0f), 6); }
        if (p.events & EV_BODY_FALL)
        {
            sfxPlay(SFX_BODY_FALL, 1.0f, rng.range(0.9f, 1.05f), pan);
            spawnDust(v3(p.x - p.facing * 0.3f, 0.03f, 0.0f), 12);
            Particle g; g.kind = 6; g.pos = v3(p.x - p.facing * 0.2f, 0.02f, 0.0f); g.maxLife = g.life = 0.45f; g.size = 1.5f; g.col = Color{ 235, 225, 205, 255 };
            parts.push_back(g);
            camShake = std::max(camShake, 0.07f);
        }
        if (p.events & EV_SPECIAL)
        {
            sfxPlay(SFX_SPECIAL, 0.8f, rng.range(0.96f, 1.06f), pan);
            Particle g; g.kind = 6; g.pos = v3(p.x, 0.02f, 0.0f); g.maxLife = g.life = 0.35f; g.size = 1.1f; g.col = p.ch->fxA;
            parts.push_back(g);
        }
        if (p.events & EV_SHOT) spawnShot(i);
        if (p.events & EV_SUPER)
        {
            superFreeze = 38; superOwner = i; superBanner = 0.0f;
            sfxPlay(SFX_CHARGE, 1.0f);
            musicDuck(0.6f);
            hitstop = 0;
        }
        if (p.events & EV_BEAM)
        {
            sfxPlay(SFX_BEAM, 1.0f, 1.0f, pan);
            whiteFlash = std::max(whiteFlash, 0.45f);
            camShake = std::max(camShake, 0.16f);
            spawnBurst(p.handsWorld(), p.ch->fxA, p.ch->fxB, 1.2f);
        }
    }
}

// ---------------------------------------------------------------------------
// Projectiles and the super beam
// ---------------------------------------------------------------------------
void Match::spawnShot(int who)
{
    Fighter& p = f[who];
    Projectile s;
    s.id = nextShotId++;
    s.owner = who;
    Vector3 h = p.handsWorld();
    s.x = h.x + p.facing * 0.30f; s.y = h.y;
    s.vx = p.facing * p.ch->shotSpeed;
    s.damage = p.ch->shotDamage;
    s.radius = 0.20f + p.ch->shotDamage * 0.0008f;
    s.a = p.ch->fxA; s.b = p.ch->fxB;
    shots.push_back(s);
    p.meter = std::min(SUPER_COST, p.meter + 3.0f);
    sfxPlay(SFX_SHOT, 0.9f, rng.range(0.95f, 1.05f), clampf(p.x / ARENA_HALF, -1.0f, 1.0f) * 0.5f);
    spawnBurst(v3(s.x, s.y, 0.1f), s.a, s.b, 0.45f);
    camShake = std::max(camShake, 0.03f);
}

void Match::updateShots()
{
    for (Projectile& s : shots)
    {
        if (s.dead) continue;
        s.age++;
        s.x += s.vx * TICK_DT;
        // tail
        Particle g; g.kind = 4;
        g.pos = v3(s.x - (s.vx > 0 ? 1 : -1) * 0.12f, s.y + rng.range(-0.08f, 0.08f), 0.1f + rng.range(-0.05f, 0.05f));
        g.vel = v3(-s.vx * 0.12f + rng.range(-0.4f, 0.4f), rng.range(-0.5f, 0.5f), 0);
        g.maxLife = g.life = rng.range(0.18f, 0.36f); g.size = s.radius * rng.range(0.6f, 1.1f); g.col = rng.chance(0.6f) ? s.a : s.b;
        parts.push_back(g);
        if (fabsf(s.x) > ARENA_HALF + 3.5f) { s.dead = true; continue; }

        Box sb{ s.x - s.radius, s.y - s.radius, s.x + s.radius, s.y + s.radius };
        // two shots cancel each other out
        for (Projectile& o : shots)
        {
            if (&o == &s || o.dead || o.owner == s.owner) continue;
            if (fabsf(o.x - s.x) < s.radius + o.radius && fabsf(o.y - s.y) < 0.6f)
            {
                s.dead = o.dead = true;
                spawnBurst(v3((s.x + o.x) * 0.5f, s.y, 0.1f), s.a, o.a, 1.0f);
                sfxPlay(SFX_EXPLODE, 0.9f);
                camShake = std::max(camShake, 0.08f);
            }
        }
        if (s.dead || phase != MP_FIGHT) continue;
        Fighter& def = f[1 - s.owner]; Fighter& att = f[s.owner];
        for (const Box& hb : def.hurt)
        {
            if (!sb.overlaps(hb)) continue;
            MoveDef m = projectileHitDef();
            m.damage = s.damage;
            int before = def.health;
            HitResult res = def.receiveHit(m, att, Vector2{ s.x, s.y });
            // the shot comes from where it is, not from where its owner stands
            bool blocked = (res == HIT_BLOCKED);
            addMeter(s.owner, before - def.health, blocked);
            hitstop = std::max(hitstop, blocked ? 4 : 8);
            hitVictim = 1 - s.owner;
            Vector3 at = v3(s.x, s.y, 0.2f);
            spawnHit(at, true, blocked, s.vx > 0 ? 1 : -1, s.a);
            spawnBurst(at, s.a, s.b, blocked ? 0.5f : 1.0f);
            sfxPlay(blocked ? SFX_BLOCK : SFX_EXPLODE, 1.0f, rng.range(0.95f, 1.05f), clampf(s.x / ARENA_HALF, -1.0f, 1.0f) * 0.5f);
            camShake = std::max(camShake, blocked ? 0.04f : 0.11f);
            if (!blocked) whiteFlash = std::max(whiteFlash, 0.12f);
            s.dead = true;
            break;
        }
    }
    shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Projectile& s) { return s.dead; }), shots.end());
}

void Match::updateBeam(int who)
{
    Fighter& o = f[who]; Fighter& def = f[1 - who];
    BeamVisual& bv = beam[who];
    if (!o.beamActive()) { bv.on = false; return; }
    const MoveDef& m = moveDef(MV_BEAM);
    int k = o.moveFrame - m.startup;
    Vector3 h = o.handsWorld();
    float dir = (float)o.facing;
    float x0 = h.x + dir * 0.22f;
    float xEnd = dir * (ARENA_HALF + 8.0f);
    Box bb{ std::min(x0, xEnd), h.y - 0.30f, std::max(x0, xEnd), h.y + 0.30f };
    bool touching = false;
    if (phase == MP_FIGHT || phase == MP_KO)
        for (const Box& hb : def.hurt) if (bb.overlaps(hb)) { touching = true; break; }
    bv.on = true; bv.x0 = x0; bv.y = h.y; bv.dir = o.facing;
    bv.a = o.ch->fxA; bv.b = o.ch->fxB;
    bv.touching = touching;
    bv.x1 = touching ? def.x - dir * 0.10f : xEnd;
    bv.power = std::min(1.0f, k / 5.0f) * std::min(1.0f, (m.active - k) / 7.0f);

    // enemy shots are vaporised
    for (Projectile& s : shots)
        if (s.owner != who && !s.dead && (s.x - x0) * dir > 0 && fabsf(s.y - h.y) < 0.6f) { s.dead = true; spawnBurst(v3(s.x, s.y, 0.1f), s.a, s.b, 0.6f); }

    camShake = std::max(camShake, 0.045f);
    // energy motes streaming along the beam
    for (int i = 0; i < 3; i++)
    {
        Particle g; g.kind = 4;
        float t = rng.unit();
        g.pos = v3(x0 + (bv.x1 - x0) * t, h.y + rng.range(-0.30f, 0.30f), 0.1f);
        g.vel = v3(dir * rng.range(6.0f, 14.0f), rng.range(-0.6f, 0.6f), 0);
        g.maxLife = g.life = rng.range(0.12f, 0.28f); g.size = rng.range(0.08f, 0.20f); g.col = rng.chance(0.5f) ? bv.a : bv.b;
        parts.push_back(g);
    }
    if (touching && phase == MP_FIGHT && (k % BEAM_TICK) == 0)
    {
        bool last = k >= m.active - BEAM_TICK;
        int before = def.health;
        Vector2 at = { def.x - dir * 0.2f, h.y };
        HitResult res = def.receiveHit(beamHitDef(last), o, at);
        bool blocked = (res == HIT_BLOCKED);
        (void)before;
        hitVictim = 1 - who;
        if (last) hitstop = std::max(hitstop, 10);
        spawnHit(v3(at.x, at.y, 0.2f), last, blocked, o.facing, bv.a);
        if (last && !blocked) { spawnBurst(v3(at.x, at.y, 0.2f), bv.a, bv.b, 1.5f); whiteFlash = std::max(whiteFlash, 0.3f); }
        sfxPlay(blocked ? SFX_BLOCK : SFX_HIT_HEAVY, blocked ? 0.6f : 0.75f, rng.range(1.0f, 1.25f), clampf(at.x / ARENA_HALF, -1.0f, 1.0f) * 0.5f);
        camShake = std::max(camShake, last ? 0.2f : 0.08f);
    }
    if (touching && (k % 2) == 0)
    {
        // splash where the beam ends
        for (int i = 0; i < 2; i++)
        {
            Particle s; s.kind = 0; s.pos = v3(bv.x1, h.y + rng.range(-0.2f, 0.2f), 0.2f);
            float ang = rng.range(-1.4f, 1.4f);
            float sp = rng.range(3.0f, 9.0f);
            s.vel = v3(-dir * cosf(ang) * sp * 0.3f + dir * rng.range(0, 2.0f), sinf(ang) * sp, rng.range(-1.0f, 1.0f));
            s.maxLife = s.life = rng.range(0.15f, 0.35f); s.size = rng.range(0.014f, 0.03f); s.col = rng.chance(0.5f) ? bv.a : bv.b;
            parts.push_back(s);
        }
    }
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------
void Match::spawnHit(Vector3 at, bool heavy, bool blocked, int dir, Color tint)
{
    Color core = blocked ? Color{ 150, 210, 255, 255 } : colorLerp(tint, WHITE, heavy ? 0.25f : 0.55f);
    Particle flash; flash.kind = 3; flash.pos = at; flash.maxLife = flash.life = heavy ? 0.22f : 0.15f;
    flash.size = blocked ? 0.34f : (heavy ? 0.95f : 0.55f); flash.col = core; flash.rot = rng.range(0, 90);
    parts.push_back(flash);
    Particle ring; ring.kind = 1; ring.pos = at; ring.maxLife = ring.life = heavy ? 0.30f : 0.22f;
    ring.size = blocked ? 0.38f : (heavy ? 1.05f : 0.60f); ring.col = blocked ? Color{ 170, 220, 255, 255 } : WHITE;
    parts.push_back(ring);
    if (heavy && !blocked)
    {
        Particle r2 = ring; r2.maxLife = r2.life = 0.42f; r2.size = 1.7f; r2.col = tint;
        parts.push_back(r2);
    }
    // sparks
    int n = blocked ? 8 : (heavy ? 26 : 13);
    for (int i = 0; i < n; i++)
    {
        Particle s; s.kind = 0; s.pos = at;
        float ang = rng.range(-1.1f, 1.1f) + (rng.chance(0.25f) ? PI : 0.0f);
        float sp = rng.range(2.5f, heavy ? 10.5f : 6.5f);
        s.vel = v3(cosf(ang) * sp * dir, sinf(ang) * sp + 1.2f, rng.range(-1.5f, 1.5f));
        s.maxLife = s.life = rng.range(0.16f, heavy ? 0.46f : 0.30f);
        s.size = rng.range(0.012f, 0.03f);
        s.col = blocked ? Color{ 160, 215, 255, 255 } : colorLerp(Color{ 255, 245, 200, 255 }, tint, rng.unit());
        parts.push_back(s);
    }
    // speed lines shooting away from the impact
    if (!blocked)
    {
        int lines = heavy ? 14 : 7;
        for (int i = 0; i < lines; i++)
        {
            Particle l; l.kind = 5; l.pos = at;
            float ang = rng.range(0.0f, 2.0f * PI);
            l.vel = v3(cosf(ang), sinf(ang), 0);
            l.maxLife = l.life = rng.range(0.10f, heavy ? 0.24f : 0.16f);
            l.size = rng.range(0.5f, heavy ? 1.9f : 1.1f);
            l.col = colorLerp(WHITE, tint, rng.unit() * 0.7f);
            parts.push_back(l);
        }
    }
}

// big energy explosion (projectile impact, special hits, K.O.)
void Match::spawnBurst(Vector3 at, Color a, Color b, float power)
{
    Particle flash; flash.kind = 3; flash.pos = at; flash.maxLife = flash.life = 0.26f; flash.size = 1.1f * power; flash.col = colorLerp(a, WHITE, 0.4f); flash.rot = rng.range(0, 90);
    parts.push_back(flash);
    for (int i = 0; i < 2; i++)
    {
        Particle ring; ring.kind = 1; ring.pos = at; ring.maxLife = ring.life = 0.28f + 0.16f * i; ring.size = (1.0f + 0.9f * i) * power; ring.col = i == 0 ? WHITE : a;
        parts.push_back(ring);
    }
    int n = (int)(16 * power) + 6;
    for (int i = 0; i < n; i++)
    {
        Particle g; g.kind = 4; g.pos = at;
        float ang = rng.range(0.0f, 2.0f * PI), sp = rng.range(1.0f, 6.5f) * power;
        g.vel = v3(cosf(ang) * sp, sinf(ang) * sp, rng.range(-1.0f, 1.0f));
        g.maxLife = g.life = rng.range(0.22f, 0.55f); g.size = rng.range(0.10f, 0.30f) * power; g.col = rng.chance(0.5f) ? a : b;
        parts.push_back(g);
    }
    for (int i = 0; i < (int)(12 * power); i++)
    {
        Particle l; l.kind = 5; l.pos = at;
        float ang = rng.range(0.0f, 2.0f * PI);
        l.vel = v3(cosf(ang), sinf(ang), 0);
        l.maxLife = l.life = rng.range(0.14f, 0.30f); l.size = rng.range(0.8f, 2.4f) * power; l.col = colorLerp(WHITE, a, rng.unit());
        parts.push_back(l);
    }
}

// motes that get sucked into a point (charging up)
void Match::spawnCharge(Vector3 at, Color a, Color b, float radius, int count)
{
    for (int i = 0; i < count; i++)
    {
        Particle g; g.kind = 4;
        float ang = rng.range(0.0f, 2.0f * PI), r = radius * rng.range(0.6f, 1.0f);
        Vector3 off = v3(cosf(ang) * r, sinf(ang) * r, rng.range(-0.3f, 0.3f));
        g.maxLife = g.life = rng.range(0.20f, 0.34f);
        g.pos = at + off;
        g.vel = Vector3Scale(off, -1.0f / g.life);
        g.size = rng.range(0.07f, 0.16f); g.col = rng.chance(0.5f) ? a : b;
        parts.push_back(g);
    }
}

void Match::spawnDust(Vector3 at, int count)
{
    for (int i = 0; i < count; i++)
    {
        Particle d; d.kind = 2;
        d.pos = at + v3(rng.range(-0.25f, 0.25f), rng.range(0.0f, 0.08f), rng.range(-0.15f, 0.25f));
        d.vel = v3(rng.range(-1.3f, 1.3f), rng.range(0.15f, 0.7f), rng.range(-0.3f, 0.3f));
        d.maxLife = d.life = rng.range(0.35f, 0.7f);
        d.size = rng.range(0.10f, 0.22f);
        d.col = Color{ 205, 195, 180, 255 };
        parts.push_back(d);
    }
}

// ---------------------------------------------------------------------------
// Per-frame updates (not tied to the 60 Hz simulation)
// ---------------------------------------------------------------------------
void Match::updateCamera(float dt)
{
    float mx = (f[0].x + f[1].x) * 0.5f;
    float sep = fabsf(f[0].x - f[1].x);
    float top = std::max(f[0].y, f[1].y);
    float dist = clampf(4.55f + sep * 0.62f, 5.3f, 8.3f) + top * 0.55f;
    Vector3 wantPos = v3(mx, 0.98f + top * 0.42f, dist);
    Vector3 wantTgt = v3(mx, 1.20f + top * 0.42f, 0.0f);
    float speed = 7.0f;

    if (phase == MP_INTRO)
    {
        // sweep in from the side while the round is announced
        float k = 1.0f - smooth01(phaseFrame / 95.0f);
        float ang = k * 0.60f;
        float d2 = dist + k * 1.4f;
        wantPos = v3(mx + sinf(ang) * d2, 0.98f + k * 0.6f, cosf(ang) * d2);
        speed = 14.0f;
    }
    else if (superFreeze > 0 && superOwner >= 0)
    {
        // dramatic push-in on the fighter charging the super
        const Fighter& o = f[superOwner];
        wantPos = v3(o.x + o.facing * 1.5f, 1.15f, 3.3f);
        wantTgt = v3(o.x + o.facing * 0.2f, 1.12f, 0.0f);
        speed = 9.0f;
    }
    else if (phase == MP_KO && !timeUp && roundWinner != 2)
    {
        const Fighter& loser = f[1 - roundWinner];
        float k = smooth01(phaseFrame / 40.0f);
        wantPos = lerp3(wantPos, v3(loser.x * 0.6f + mx * 0.4f, 0.9f, dist * 0.80f), k);
        wantTgt = lerp3(wantTgt, v3(loser.x * 0.6f + mx * 0.4f, 0.85f, 0), k);
    }
    else if (phase == MP_WINPOSE && roundWinner != 2)
    {
        const Fighter& w = f[roundWinner];
        float k = smooth01(phaseFrame / 50.0f);
        float side = w.facing > 0 ? 1.0f : -1.0f;
        wantPos = lerp3(wantPos, v3(w.x + side * 0.9f, 1.15f, 3.9f), k);
        wantTgt = lerp3(wantTgt, v3(w.x + side * 0.15f, 1.15f, 0), k);
        speed = 4.0f;
    }

    if (camSnap) { camPos = wantPos; camTarget = wantTgt; camSnap = false; }
    else
    {
        float a = 1.0f - expf(-speed * dt);
        camPos = lerp3(camPos, wantPos, a);
        camTarget = lerp3(camTarget, wantTgt, a);
    }
    camShake *= expf(-9.0f * dt);
    fovKick *= expf(-10.0f * dt);
    Vector3 sh = v3(rng.range(-1, 1) * camShake, rng.range(-1, 1) * camShake, 0);
    camera.position = camPos + sh;
    camera.target = camTarget + Vector3Scale(sh, 0.6f);
    camera.up = v3(0, 1, 0);
    camera.fovy = 34.0f - fovKick * 1.6f;
    camera.projection = CAMERA_PERSPECTIVE;
}

void Match::frame(float dt)
{
    hudTime += dt;
    updateCamera(dt);
    float pdt = (phase == MP_KO && !timeUp && phaseFrame < 84) ? dt * 0.4f : dt;
    for (Particle& p : parts)
    {
        p.life -= pdt;
        if (p.kind != 5) p.pos = p.pos + Vector3Scale(p.vel, pdt);
        if (p.kind == 0) { p.vel.y -= 14.0f * pdt; p.vel = Vector3Scale(p.vel, expf(-2.5f * pdt)); }
        if (p.kind == 2) { p.vel = Vector3Scale(p.vel, expf(-3.0f * pdt)); }
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0.0f; }), parts.end());
    for (int i = 0; i < 2; i++)
    {
        for (TrailPoint& t : trail[i]) t.age += pdt;
        trail[i].erase(std::remove_if(trail[i].begin(), trail[i].end(), [](const TrailPoint& t) { return t.age > 0.20f; }), trail[i].end());
    }

    for (int i = 0; i < 2; i++)
    {
        float target = (float)f[i].health / (float)std::max(f[i].maxHealth, 1);
        if (target < shownHealth[i] - 0.0001f) ghostDelay[i] = 0.55f;
        shownHealth[i] = target;
        if (ghostDelay[i] > 0) ghostDelay[i] -= dt;
        else ghostHealth[i] = std::max(shownHealth[i], ghostHealth[i] - dt * 0.55f);
        if (ghostHealth[i] < shownHealth[i]) ghostHealth[i] = shownHealth[i];
        if (comboTimer[i] > 0) comboTimer[i] -= dt;
        comboPop[i] = std::max(0.0f, comboPop[i] - dt * 5.0f);
    }
    if (annTime < annDur) annTime += dt;
    koFlash = std::max(0.0f, koFlash - dt * 2.2f);
    whiteFlash = std::max(0.0f, whiteFlash - dt * 2.8f);
    superBanner += dt;
    bool dark = superFreeze > 0 || beam[0].on || beam[1].on;
    dim = approach(dim, dark ? 0.30f : 1.0f, dark ? 14.0f : 5.0f, dt);
    if (!dark && phase == MP_FIGHT && superBanner > 0.5f && superBanner < 3.0f) musicDuck(0.0f);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void Match::render(Renderer& r)
{
    float mx = (f[0].x + f[1].x) * 0.5f;
    r.beginShadows(v3(mx, 0.9f, 0.0f), 5.6f);
    for (Fighter& p : f) r.shadowSkinned(p.ch->gpu, p.skin, p.worldMatrix());
    r.endShadows();

    float baseExposure = r.env.exposure;
    r.env.exposure = baseExposure * dim;               // supers darken the world, not the fighters
    r.beginScene(camera, true);
    r.drawGround(ARENA_HALF + 1.0f, stage && stage->procedural);
    if (stage && stage->loaded) stage->draw(r);
    r.setExposure(baseExposure * (0.55f + 0.45f * dim));
    for (int i = 0; i < 2; i++)
    {
        Fighter& p = f[i];
        SurfaceParams sp;
        sp.flash = Vector4{ 1.0f, 0.96f, 0.86f, p.flash * 0.40f };
        sp.rim = Vector4{ 1.0f, 0.80f, 0.55f, 0.35f };
        sp.saturation = 1.18f;
        // energy lights the fighters up: own aura while charging / firing, enemy beam when caught in it
        const MoveDef* m = p.move != MV_NONE ? &moveDef(p.move) : nullptr;
        if (m && m->special && p.attacking())
            sp.rim = Vector4{ p.ch->fxA.r / 255.0f, p.ch->fxA.g / 255.0f, p.ch->fxA.b / 255.0f, p.move == MV_BEAM ? 3.2f : 1.8f };
        else if (beam[1 - i].on)
            sp.rim = Vector4{ beam[1 - i].a.r / 255.0f, beam[1 - i].a.g / 255.0f, beam[1 - i].a.b / 255.0f, 2.6f };
        r.drawSkinned(p.ch->gpu, p.skin, p.worldMatrix(), sp);
    }
    r.endScene();
    r.env.exposure = baseExposure;
}

static void glowBlob(const UI& ui, Vector2 c, float r, Color col)
{
    DrawTexturePro(ui.glow, Rectangle{ 0, 0, (float)ui.glow.width, (float)ui.glow.height },
                   Rectangle{ c.x - r, c.y - r, 2 * r, 2 * r }, Vector2{ 0, 0 }, 0, col);
}

// thick line with independent colours at both ends (screen space)
static void taperQuad(Vector2 a, Vector2 b, float wa, float wb, Color ca, Color cb)
{
    Vector2 d = Vector2Subtract(b, a);
    float len = Vector2Length(d);
    if (len < 0.001f) return;
    Vector2 n = { -d.y / len, d.x / len };
    Vector2 a0 = Vector2Add(a, Vector2Scale(n, wa)), a1 = Vector2Subtract(a, Vector2Scale(n, wa));
    Vector2 b0 = Vector2Add(b, Vector2Scale(n, wb)), b1 = Vector2Subtract(b, Vector2Scale(n, wb));
    rlBegin(RL_TRIANGLES);
    // both windings, so the quad is visible whatever its orientation
    for (int pass = 0; pass < 2; pass++)
    {
        Vector2 v[6] = { a0, a1, b1, a0, b1, b0 };
        Color c[6] = { ca, ca, cb, ca, cb, cb };
        for (int i = 0; i < 6; i++)
        {
            int k = pass == 0 ? i : 5 - i;
            rlColor4ub(c[k].r, c[k].g, c[k].b, c[k].a);
            rlVertex2f(v[k].x, v[k].y);
        }
    }
    rlEnd();
}

void Match::drawSuperBanner(UI& ui)
{
    if (superBanner > 1.55f || superOwner < 0) return;
    const Fighter& o = f[superOwner];
    float t = superBanner;
    float in = easeOutCubic(t / 0.18f), out = saturate((t - 1.30f) / 0.25f);
    float a = in * (1.0f - out);
    bool right = (superOwner == 1);
    float y = 790.0f, h = 190.0f;
    float slide = (1.0f - in) * 500.0f * (right ? 1.0f : -1.0f);
    Color ca = o.ch->fxA, dark = Color{ 6, 8, 16, 255 };
    // band
    ui.rectGradH(0, y, ui.W * 0.5f, h, withAlpha(right ? dark : colorLerp(dark, ca, 0.55f), a * 0.92f), withAlpha(colorLerp(dark, ca, 0.25f), a * 0.92f));
    ui.rectGradH(ui.W * 0.5f, y, ui.W * 0.5f, h, withAlpha(colorLerp(dark, ca, 0.25f), a * 0.92f), withAlpha(right ? colorLerp(dark, ca, 0.55f) : dark, a * 0.92f));
    ui.rect(0, y, ui.W, 6, withAlpha(ca, a));
    ui.rect(0, y + h - 6, ui.W, 6, withAlpha(ca, a));
    // streaking lines
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 14; i++)
    {
        float ly = y + 14 + (i * 53 % (int)(h - 28));
        float lx = fmodf(i * 371.0f + hudTime * (1800.0f + i * 90.0f) * (right ? -1.0f : 1.0f), ui.W + 600.0f);
        if (lx < 0) lx += ui.W + 600.0f;
        ui.rectGradH(lx - 600, ly, 420, 3, withAlpha(ca, 0), withAlpha(WHITE, a * 0.5f));
    }
    EndBlendMode();
    // portrait cut-in
    if (o.ch->photo.id)
    {
        Texture2D ph = o.ch->photo;
        float pw = 430.0f, phh = pw * ph.height / ph.width;
        float px = (right ? ui.W - 80.0f - pw : 80.0f) + slide;
        BeginScissorMode(0, (int)(y * ui.s), GetScreenWidth(), (int)(h * ui.s));
        Rectangle src = { 0, 0, (float)ph.width * (right ? -1.0f : 1.0f), (float)ph.height };
        ui.texture(ph, src, px, y - phh * 0.20f, pw, phh, withAlpha(WHITE, a));
        EndScissorMode();
    }
    float tx = right ? ui.W - 560.0f - slide * -0.6f : 560.0f + slide * 0.6f;
    ui.textOutline(ui.title, o.ch->superName.c_str(), tx, y + 18, 124, withAlpha(WHITE, a), withAlpha(colorLerp(ca, BLACK, 0.6f), a), 6, right ? AL_RIGHT : AL_LEFT, 3);
    ui.text(ui.heavy, "SUPER MOVE", tx + (right ? -6 : 6), y + 138, 36, withAlpha(o.ch->fxB, a), right ? AL_RIGHT : AL_LEFT, 6);
}

void Match::drawMeter(UI& ui, int side)
{
    const Fighter& p = f[side];
    float w = 420.0f, h = 22.0f, y = ui.H - 64.0f;
    float x = side == 0 ? 70.0f : ui.W - 70.0f - w;
    float skew = side == 0 ? 10.0f : -10.0f;
    float k = saturate(p.meter / SUPER_COST);
    bool full = p.meter >= SUPER_COST;
    Color a = p.ch->fxA, b = p.ch->fxB;
    ui.para(x - 5, y - 5, w + 10, h + 10, skew * 1.4f, Color{ 8, 10, 16, 215 });
    ui.para(x, y, w, h, skew, Color{ 30, 32, 46, 255 });
    float fw = w * k;
    float fx = side == 0 ? x : x + w - fw;
    float pulse = full ? 0.75f + 0.25f * sinf(hudTime * 10.0f) : 1.0f;
    ui.paraGradH(fx, y, fw, h, skew, withAlpha(side == 0 ? a : b, pulse), withAlpha(side == 0 ? b : a, pulse));
    ui.para(fx + skew * 0.5f, y, fw, h * 0.45f, skew * 0.5f, Color{ 255, 255, 255, 50 });
    for (int i = 1; i < 4; i++) ui.para(x + w * i / 4.0f - 1.5f, y, 3, h, skew, Color{ 8, 10, 16, 200 });
    ui.paraLines(x, y, w, h, skew, 2.5f, full ? colorLerp(WHITE, b, 0.5f + 0.5f * sinf(hudTime * 10.0f)) : Color{ 200, 205, 220, 255 });
    Align al = side == 0 ? AL_LEFT : AL_RIGHT;
    float tx = side == 0 ? x + 6 : x + w - 6;
    if (full)
    {
        ui.glowAt(side == 0 ? x + w : x, y + h * 0.5f, 60, withAlpha(a, 0.5f + 0.3f * sinf(hudTime * 10.0f)));
        ui.textOutline(ui.heavy, "SUPER READY!", tx, y - 48, 40, colorLerp(WHITE, b, 0.5f + 0.5f * sinf(hudTime * 10.0f)), Color{ 20, 10, 6, 255 }, 3, al, 2);
    }
    else ui.textShadow(ui.body, "SUPER", tx, y - 38, 28, Color{ 190, 196, 214, 255 }, al, 2, Color{ 0, 0, 0, 200 }, 3);
}

void Match::drawHealthBar(UI& ui, int side)
{
    const Fighter& p = f[side];
    float cx = ui.W * 0.5f;
    float barW = std::min(720.0f, ui.W * 0.5f - 230.0f), barH = 40.0f, y = 62.0f;
    float inner = cx + (side == 0 ? -92.0f : 92.0f);          // end near the timer
    float dir = side == 0 ? -1.0f : 1.0f;                     // bar grows away from the centre
    float skew = side == 0 ? 16.0f : -16.0f;
    float x0 = side == 0 ? inner - barW : inner;              // left edge of the bar

    // backing plate
    ui.para(x0 - 7, y - 7, barW + 14, barH + 14, skew * 1.35f, Color{ 8, 10, 16, 215 });
    ui.para(x0, y, barW, barH, skew, Color{ 46, 14, 18, 255 });

    float hp = saturate(shownHealth[side]), ghost = saturate(ghostHealth[side]);
    auto seg = [&](float from, float to, Color cNear, Color cFar) {
        if (to <= from) return;
        // `from`/`to` are fractions measured from the centre end
        float a = inner + dir * barW * from, b = inner + dir * barW * to;
        float l = std::min(a, b), w = fabsf(b - a);
        Color cl = side == 0 ? cFar : cNear, cr = side == 0 ? cNear : cFar;
        ui.paraGradH(l, y, w, barH, skew, cl, cr);
    };
    seg(hp, ghost, Color{ 235, 60, 50, 255 }, Color{ 235, 60, 50, 255 });
    bool low = hp < 0.25f;
    float pulse = low ? (0.75f + 0.25f * sinf(hudTime * 12.0f)) : 1.0f;
    Color nearC = low ? Color{ (unsigned char)(255 * pulse), 70, 40, 255 } : Color{ 255, 226, 80, 255 };
    Color farC = low ? Color{ (unsigned char)(230 * pulse), 40, 30, 255 } : Color{ 255, 150, 30, 255 };
    seg(0.0f, hp, nearC, colorLerp(nearC, farC, hp));
    // gloss + frame
    ui.para(x0 + skew * 0.5f, y, barW, barH * 0.45f, skew * 0.5f, Color{ 255, 255, 255, 38 });
    ui.paraLines(x0, y, barW, barH, skew, 3.0f, Color{ 235, 238, 245, 255 });

    // portrait
    float outer = inner + dir * (barW + 66.0f);
    float pr = 54.0f;
    Color accent = side == 0 ? Color{ 235, 64, 52, 255 } : Color{ 60, 130, 245, 255 };
    ui.diamond(outer, y + 24, pr + 9, Color{ 8, 10, 16, 235 });
    ui.diamond(outer, y + 24, pr + 4, accent);
    ui.diamond(outer, y + 24, pr - 1, Color{ 24, 26, 36, 255 });
    if (p.ch)
    {
        float ps = pr * 1.42f;
        BeginScissorMode((int)((outer - ps * 0.5f) * ui.s), (int)((y + 24 - ps * 0.5f) * ui.s), (int)(ps * ui.s), (int)(ps * ui.s));
        float flip = side == 0 ? 1.0f : -1.0f;
        if (p.ch->photo.id)
        {
            // head region of the portrait photo
            Texture2D t = p.ch->photo;
            Rectangle src = { t.width * 0.12f, t.height * 0.10f, t.width * 0.76f * flip, t.width * 0.76f };
            if (flip < 0) src.x = t.width * 0.12f;
            ui.texture(t, src, outer - ps * 0.5f, y + 24 - ps * 0.5f, ps, ps, WHITE);
        }
        else if (p.ch->portrait.texture.id)
        {
            Texture2D t = p.ch->portrait.texture;
            Rectangle src = { 0, 0, (float)t.width * flip, -(float)t.height };
            ui.texture(t, src, outer - ps * 0.5f, y + 24 - ps * 0.5f, ps, ps, WHITE);
        }
        EndScissorMode();
    }

    // name + tag
    float nameX = inner + dir * (barW - 6.0f);
    const char* tag = cpu[side] ? "CPU" : (side == 0 ? "P1" : "P2");
    ui.textShadow(ui.heavy, p.ch ? p.ch->name.c_str() : "", nameX, y + barH + 12, 44, WHITE, side == 0 ? AL_LEFT : AL_RIGHT, 3);
    float nw = ui.measure(ui.heavy, p.ch ? p.ch->name.c_str() : "", 44);
    ui.textShadow(ui.body, tag, nameX - dir * (nw + 16.0f), y + barH + 22, 28, accent, side == 0 ? AL_LEFT : AL_RIGHT, 2);

    // round markers
    for (int i = 0; i < roundsToWin; i++)
    {
        float px = inner + dir * (26.0f + i * 40.0f), py = y + barH + 34.0f;
        ui.diamond(px, py, 15, Color{ 8, 10, 16, 230 });
        ui.diamond(px, py, 10, i < wins[side] ? Color{ 255, 205, 50, 255 } : Color{ 70, 74, 90, 255 });
    }
}

void Match::drawOverlay(UI& ui)
{
    float pxPerM = GetScreenHeight() / (2.0f * Vector3Distance(camera.position, camera.target) * tanf(camera.fovy * 0.5f * DEG2RAD));
    auto S = [&](Vector3 p) { return GetWorldToScreen(p, camera); };

    // ---- attack trails ----
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 2; i++)
    {
        const std::vector<TrailPoint>& t = trail[i];
        size_t n = t.size();
        for (size_t k = 1; k < n; k++)
        {
            float a0 = saturate(1.0f - t[k - 1].age / 0.20f), a1 = saturate(1.0f - t[k].age / 0.20f);
            float w = 0.085f * pxPerM;
            taperQuad(S(t[k - 1].p), S(t[k].p), w * a0 * a0, w * a1 * a1, withAlpha(trailColor[i], a0 * 0.55f), withAlpha(colorLerp(trailColor[i], WHITE, 0.4f), a1 * 0.75f));
        }
    }

    // ---- super beam ----
    for (int i = 0; i < 2; i++)
    {
        const BeamVisual& b = beam[i];
        if (!b.on || b.power <= 0.01f) continue;
        Vector2 p0 = S(v3(b.x0, b.y, 0.1f)), p1 = S(v3(b.x1, b.y, 0.1f));
        float flick = 0.88f + 0.12f * sinf(hudTime * 70.0f);
        float w = 0.34f * pxPerM * b.power * flick;
        taperQuad(p0, p1, w * 2.6f, w * 2.9f, withAlpha(b.a, 0.16f), withAlpha(b.a, 0.16f));
        taperQuad(p0, p1, w * 1.7f, w * 1.9f, withAlpha(b.a, 0.38f), withAlpha(b.a, 0.38f));
        taperQuad(p0, p1, w * 1.05f, w * 1.15f, withAlpha(b.b, 0.80f), withAlpha(b.b, 0.80f));
        taperQuad(p0, p1, w * 0.55f, w * 0.60f, withAlpha(WHITE, 0.95f), withAlpha(WHITE, 0.95f));
        // energy ripples travelling down the beam
        float len = Vector2Distance(p0, p1);
        Vector2 dir = Vector2Scale(Vector2Subtract(p1, p0), 1.0f / std::max(len, 1.0f));
        for (int k = 0; k < 9; k++)
        {
            float u = fmodf(k / 9.0f + hudTime * 2.6f, 1.0f);
            Vector2 c = Vector2Add(p0, Vector2Scale(dir, u * len));
            float off = sinf(k * 2.4f) * w * 0.9f;
            Vector2 a = { c.x, c.y + off }, e = Vector2Add(a, Vector2Scale(dir, w * 2.4f));
            taperQuad(a, e, w * 0.02f, w * 0.10f, withAlpha(WHITE, 0.0f), withAlpha(WHITE, 0.7f));
        }
        glowBlob(ui, p0, w * 4.2f, withAlpha(b.a, 0.75f));
        glowBlob(ui, p0, w * 2.6f, withAlpha(b.b, 0.95f));
        glowBlob(ui, p0, w * 1.5f, WHITE);
        if (b.touching)
        {
            float r = w * (4.0f + 1.2f * sinf(hudTime * 55.0f));
            glowBlob(ui, p1, r * 1.5f, withAlpha(b.a, 0.75f));
            glowBlob(ui, p1, r, withAlpha(b.b, 0.9f));
            glowBlob(ui, p1, r * 0.55f, WHITE);
        }
    }

    // ---- energy shots ----
    for (const Projectile& s : shots)
    {
        Vector2 c = S(v3(s.x, s.y, 0.1f));
        float r = s.radius * pxPerM * (1.0f + 0.12f * sinf(s.age * 0.9f));
        float grow = std::min(1.0f, s.age / 4.0f);
        r *= 0.5f + 0.5f * grow;
        float dir = s.vx > 0 ? 1.0f : -1.0f;
        // comet tail
        taperQuad(Vector2{ c.x - dir * r * 5.0f, c.y }, c, r * 0.1f, r * 1.0f, withAlpha(s.a, 0.0f), withAlpha(s.a, 0.55f));
        taperQuad(Vector2{ c.x - dir * r * 2.8f, c.y }, c, r * 0.1f, r * 0.6f, withAlpha(s.b, 0.0f), withAlpha(s.b, 0.75f));
        glowBlob(ui, c, r * 3.6f, withAlpha(s.a, 0.55f));
        glowBlob(ui, c, r * 2.2f, withAlpha(s.a, 0.9f));
        glowBlob(ui, c, r * 1.4f, withAlpha(s.b, 1.0f));
        glowBlob(ui, c, r * 0.8f, WHITE);
        for (int k = 0; k < 4; k++)
        {
            float ang = s.age * 0.35f + k * PI * 0.25f;
            float len = r * ((k % 2) ? 1.5f : 2.3f);
            Vector2 d = { cosf(ang) * len, sinf(ang) * len };
            taperQuad(Vector2Subtract(c, d), Vector2Add(c, d), r * 0.06f, r * 0.06f, withAlpha(WHITE, 0.55f), withAlpha(WHITE, 0.55f));
        }
    }
    // glow gathering in the hands while a shot / the super is charged
    for (int i = 0; i < 2; i++)
    {
        const Fighter& p = f[i];
        if (p.state != FS_ATTACK) continue;
        float k = 0.0f;
        if (p.move == MV_FIREBALL && p.moveFrame < 13) k = p.moveFrame / 13.0f * 0.55f;
        if (p.move == MV_BEAM && p.moveFrame < 46) k = 0.3f + 0.9f * (p.moveFrame / 46.0f);
        if (k <= 0.0f) continue;
        Vector2 c = S(p.handsWorld());
        float r = 0.34f * pxPerM * k * (0.9f + 0.1f * sinf(hudTime * 60.0f));
        glowBlob(ui, c, r * 2.6f, withAlpha(p.ch->fxA, 0.6f));
        glowBlob(ui, c, r * 1.5f, withAlpha(p.ch->fxB, 0.9f));
        glowBlob(ui, c, r * 0.7f, WHITE);
    }
    EndBlendMode();

    // ---- particles ----
    for (const Particle& p : parts)
    {
        float k = saturate(p.life / p.maxLife);
        Vector2 sp = S(p.pos);
        if (p.kind == 0)
        {
            Vector2 tail = S(p.pos - Vector3Scale(p.vel, 0.035f));
            BeginBlendMode(BLEND_ADDITIVE);
            DrawLineEx(tail, sp, std::max(1.5f, p.size * pxPerM * (0.5f + k)), withAlpha(p.col, k));
            EndBlendMode();
        }
        else if (p.kind == 1)
        {
            float r = p.size * pxPerM * (0.25f + 0.75f * easeOutCubic(1.0f - k));
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRing(sp, r * (1.0f - 0.16f * k), r, 0, 360, 40, withAlpha(p.col, k * 0.8f));
            EndBlendMode();
        }
        else if (p.kind == 2)
        {
            float r = p.size * pxPerM * (1.0f + 1.6f * (1.0f - k));
            glowBlob(ui, sp, r, withAlpha(p.col, k * 0.38f));
        }
        else if (p.kind == 3)
        {
            float r = p.size * pxPerM * (0.6f + 0.6f * (1.0f - k));
            BeginBlendMode(BLEND_ADDITIVE);
            glowBlob(ui, sp, r, withAlpha(p.col, k));
            glowBlob(ui, sp, r * 0.45f, withAlpha(WHITE, k));
            for (int i = 0; i < 4; i++)
            {
                float ang = (p.rot + i * 45.0f) * DEG2RAD;
                float len = r * ((i % 2) ? 0.9f : 1.7f);
                Vector2 d = { cosf(ang) * len, sinf(ang) * len };
                taperQuad(sp, Vector2Add(sp, d), std::max(2.0f, r * 0.07f * k), 0.5f, withAlpha(WHITE, k), withAlpha(p.col, 0));
                taperQuad(sp, Vector2Subtract(sp, d), std::max(2.0f, r * 0.07f * k), 0.5f, withAlpha(WHITE, k), withAlpha(p.col, 0));
            }
            EndBlendMode();
        }
        else if (p.kind == 4)
        {
            float r = p.size * pxPerM * (0.4f + 0.6f * k);
            BeginBlendMode(BLEND_ADDITIVE);
            glowBlob(ui, sp, r, withAlpha(p.col, k * 0.85f));
            glowBlob(ui, sp, r * 0.4f, withAlpha(WHITE, k * 0.8f));
            EndBlendMode();
        }
        else if (p.kind == 5)
        {
            float u = 1.0f - k;
            Vector2 d = { p.vel.x, -p.vel.y };
            float r0 = p.size * pxPerM * (0.15f + 0.75f * easeOutCubic(u)), r1 = p.size * pxPerM * (0.35f + 0.95f * easeOutCubic(u));
            BeginBlendMode(BLEND_ADDITIVE);
            taperQuad(Vector2Add(sp, Vector2Scale(d, r0)), Vector2Add(sp, Vector2Scale(d, r1)), 0.5f, std::max(1.5f, 0.018f * pxPerM * k), withAlpha(p.col, 0), withAlpha(p.col, k));
            EndBlendMode();
        }
        else if (p.kind == 6)
        {
            // ring lying on the ground
            float r = p.size * (0.15f + 0.85f * easeOutCubic(1.0f - k));
            BeginBlendMode(BLEND_ADDITIVE);
            Vector2 prev{};
            for (int i = 0; i <= 28; i++)
            {
                float ang = i / 28.0f * 2.0f * PI;
                Vector2 q = S(p.pos + v3(cosf(ang) * r, 0, sinf(ang) * r * 0.9f));
                if (i > 0) DrawLineEx(prev, q, std::max(2.0f, 0.03f * pxPerM * k), withAlpha(p.col, k * 0.7f));
                prev = q;
            }
            EndBlendMode();
        }
    }

    // ---- debug: collision boxes ----
    if (debugBoxes)
    {
        auto drawBox = [&](const Box& b, Color c) {
            Vector2 a = S(v3(b.x0, b.y1, 0)), d = S(v3(b.x1, b.y0, 0));
            Rectangle rc = { a.x, a.y, d.x - a.x, d.y - a.y };
            DrawRectangleRec(rc, withAlpha(c, 0.22f));
            DrawRectangleLinesEx(rc, 2, c);
        };
        for (const Fighter& p : f)
        {
            for (const Box& b : p.hurt) drawBox(b, Color{ 60, 230, 110, 255 });
            drawBox(Box{ p.x - PUSH_HALF, p.y, p.x + PUSH_HALF, p.y + 0.5f }, Color{ 80, 150, 255, 255 });
            if (p.hitActive) drawBox(p.hit, Color{ 255, 50, 50, 255 });
        }
        for (const Projectile& s : shots) drawBox(Box{ s.x - s.radius, s.y - s.radius, s.x + s.radius, s.y + s.radius }, Color{ 255, 50, 50, 255 });
        for (const BeamVisual& b : beam) if (b.on) drawBox(Box{ std::min(b.x0, b.x1), b.y - 0.30f, std::max(b.x0, b.x1), b.y + 0.30f }, Color{ 255, 50, 50, 255 });
        ui.text(ui.body, "F1: collision boxes   green = hurt box   red = hit box   blue = push box", ui.W * 0.5f, ui.H - 118, 28, WHITE, AL_CENTER);
    }

    ui.drawVignette(0.28f + (1.0f - dim) * 0.6f);
    if (whiteFlash > 0) ui.fade(whiteFlash, WHITE);
    if (koFlash > 0) ui.fade(koFlash * 0.75f, WHITE);

    // ---- HUD ----
    float cx = ui.W * 0.5f;
    ui.rectGradV(0, 0, ui.W, 190, Color{ 0, 0, 0, 120 }, Color{ 0, 0, 0, 0 });
    drawHealthBar(ui, 0);
    drawHealthBar(ui, 1);
    drawMeter(ui, 0);
    drawMeter(ui, 1);

    // timer
    ui.diamond(cx, 84, 84, Color{ 8, 10, 16, 235 });
    ui.diamond(cx, 84, 76, Color{ 235, 238, 245, 255 });
    ui.diamond(cx, 84, 70, Color{ 20, 22, 32, 255 });
    int secs = (int)ceilf(std::max(0.0f, timeLeft));
    bool hurry = secs <= 10 && phase == MP_FIGHT;
    Color tc = hurry ? colorLerp(Color{ 255, 70, 50, 255 }, WHITE, 0.5f + 0.5f * sinf(hudTime * 10.0f)) : Color{ 255, 220, 90, 255 };
    ui.textShadow(ui.title, TextFormat("%02d", secs), cx - 4, 40, 86, tc, AL_CENTER, 3);

    // combo counters
    for (int i = 0; i < 2; i++)
    {
        if (comboTimer[i] <= 0 || comboCount[i] < 2) continue;
        float a = saturate(comboTimer[i] / 0.35f);
        float sz = 120.0f * (1.0f + 0.35f * comboPop[i]);
        float x = i == 0 ? 96.0f : ui.W - 96.0f;
        Align al = i == 0 ? AL_LEFT : AL_RIGHT;
        Color c = withAlpha(colorLerp(Color{ 255, 220, 70, 255 }, Color{ 255, 90, 40, 255 }, saturate((comboCount[i] - 2) / 5.0f)), a);
        ui.textOutline(ui.title, TextFormat("%d HITS", comboCount[i]), x, 300 - (sz - 120.0f) * 0.5f, sz, c, withAlpha(Color{ 20, 8, 4, 255 }, a), 5, al);
        ui.textShadow(ui.heavy, TextFormat("%d DAMAGE", comboDamage[i]), x + (i == 0 ? 6 : -6), 420, 42, withAlpha(WHITE, a), al, 3);
    }

    drawSuperBanner(ui);

    // announcements
    if (annTime < annDur && !annText.empty())
    {
        float t = annTime, left = annDur - annTime;
        float in = saturate(t / 0.22f);
        float a = std::min(in, saturate(left / 0.25f));
        float scale = 1.0f + (1.0f - easeOutBack(in)) * 1.4f + (left < 0.25f ? (0.25f - left) * 1.2f : 0.0f);
        bool big = annText.size() <= 8;
        float size = (big ? 250.0f : 150.0f) * scale;
        float maxW = ui.W * 0.92f;
        float w = ui.measure(ui.title, annText.c_str(), size, 2);
        if (w > maxW) size *= maxW / w;
        float cy = 455.0f;
        ui.rectGradH(0, cy - 150, cx, 300, Color{ 0, 0, 0, 0 }, Color{ 0, 0, 0, (unsigned char)(120 * a) });
        ui.rectGradH(cx, cy - 150, ui.W - cx, 300, Color{ 0, 0, 0, (unsigned char)(120 * a) }, Color{ 0, 0, 0, 0 });
        ui.textPop(ui.title, annText.c_str(), cx, cy, size, withAlpha(annColor, a), withAlpha(Color{ 16, 8, 8, 255 }, a), 7, 2);
        if (!annSub.empty())
            ui.textOutline(ui.heavy, annSub.c_str(), cx, cy + size * 0.42f, 82, withAlpha(WHITE, a), withAlpha(Color{ 160, 30, 20, 255 }, a), 4, AL_CENTER, 4);
    }
}
