// ============================================================================
//  TUF FIGHTER - anims.cpp
//  Hand-authored procedural animations (key poses + easing).
// ============================================================================
#include "anims.h"

namespace
{
    enum Ease { E_LIN, E_OUT, E_IN, E_INOUT };

    struct Key
    {
        float f;      // frame
        Pose  p;
        Ease  e;      // easing used to arrive at this key
    };

    struct Track
    {
        const char* name = "";
        std::vector<Key> keys;
        bool  loop = false;
        float length = 0.0f;
        std::function<Pose(float)> proc;   // fully procedural animations (walk cycles, idle, ...)
    };

    Track tracks[ANIM_COUNT];
    Pose STANCE, CROUCH, AIR;

    float applyEase(Ease e, float t)
    {
        switch (e)
        {
        case E_OUT:   return easeOutCubic(t);
        case E_IN:    return easeInCubic(t);
        case E_INOUT: return smooth01(t);
        default:      return t;
        }
    }

    Pose evalKeys(const std::vector<Key>& k, float f)
    {
        if (k.empty()) return STANCE;
        if (f <= k.front().f) return k.front().p;
        if (f >= k.back().f) return k.back().p;
        for (size_t i = 1; i < k.size(); i++)
        {
            if (f <= k[i].f)
            {
                float span = k[i].f - k[i - 1].f;
                float t = span > 1e-5f ? (f - k[i - 1].f) / span : 1.0f;
                return poseLerp(k[i - 1].p, k[i].p, applyEase(k[i].e, t));
            }
        }
        return k.back().p;
    }

    void addTrack(AnimId id, const char* name, std::vector<Key> keys, bool loop = false)
    {
        tracks[id].name = name;
        tracks[id].length = keys.empty() ? 0.0f : keys.back().f;
        tracks[id].keys = std::move(keys);
        tracks[id].loop = loop;
    }
    void addProc(AnimId id, const char* name, float length, bool loop, std::function<Pose(float)> fn)
    {
        tracks[id].name = name;
        tracks[id].length = length;
        tracks[id].loop = loop;
        tracks[id].proc = std::move(fn);
    }

    // ------------------------------------------------------------------
    // Base poses
    // ------------------------------------------------------------------
    Pose makeStance()
    {
        Pose p;
        p.hipsPos = v3(0.0f, -0.085f, -0.02f);
        p.hipsRot = v3(5, -42, 0);
        p.chestRot = v3(11, -30, 0);
        p.headRot = v3(4, -10, 0);
        p.handL = v3(0.05f, 1.30f, 0.42f);   p.elbowL = v3(0.55f, -1.0f, -0.25f);
        p.handR = v3(-0.13f, 1.27f, 0.13f);  p.elbowR = v3(-0.60f, -1.0f, -0.45f);
        p.footL = v3(0.10f, 0.0f, 0.25f);    p.kneeL = v3(0.12f, 0.0f, 1.0f);   p.footYawL = -8;
        p.footR = v3(-0.14f, 0.025f, -0.27f); p.kneeR = v3(-0.80f, 0.0f, 0.60f); p.footYawR = -52; p.footPitchR = 14;
        return p;
    }

    Pose makeCrouch()
    {
        Pose p = makeStance();
        p.hipsPos = v3(0.0f, -0.41f, -0.07f);
        p.hipsRot = v3(14, -42, 0);
        p.chestRot = v3(27, -30, 0);
        p.headRot = v3(-6, -10, 0);
        p.handL = v3(0.05f, 0.95f, 0.42f);
        p.handR = v3(-0.13f, 0.91f, 0.16f);
        p.footR = v3(-0.14f, 0.055f, -0.27f); p.footPitchR = 34;
        p.kneeL = v3(0.40f, 0.0f, 1.0f);
        p.kneeR = v3(-1.0f, 0.0f, 0.40f);
        return p;
    }

    Pose makeAir()
    {
        Pose p = makeStance();
        p.hipsPos = v3(0.0f, 0.0f, 0.0f);
        p.hipsRot = v3(10, -42, 0);
        p.chestRot = v3(18, -30, 0);
        p.handL = v3(0.14f, 1.30f, 0.34f);
        p.handR = v3(-0.20f, 1.22f, 0.12f);
        p.footL = v3(0.10f, 0.32f, 0.24f);   p.kneeL = v3(0.25f, 0.2f, 1.0f); p.footPitchL = 25;
        p.footR = v3(-0.13f, 0.26f, -0.08f); p.kneeR = v3(-0.6f, 0.2f, 0.8f); p.footPitchR = 35;
        return p;
    }

    // ------------------------------------------------------------------
    // Procedural loops
    // ------------------------------------------------------------------
    Pose idlePose(float f)
    {
        float a = f / 72.0f * 2.0f * PI;
        Pose p = STANCE;
        float b = sinf(a), b2 = sinf(a * 2.0f);
        p.hipsPos.y += 0.014f * b;
        p.hipsPos.z += 0.008f * sinf(a + 1.2f);
        p.chestRot.x += 1.6f * b;
        p.chestRot.y += 1.5f * sinf(a + 0.8f);
        p.handL.y += 0.014f * sinf(a + 0.5f) + 0.006f * b2;
        p.handL.z += 0.016f * sinf(a + 1.9f);
        p.handR.y += 0.012f * sinf(a + 0.9f);
        p.headRot.x += 0.8f * sinf(a + 0.4f);
        return p;
    }

    // Walking: each foot is planted for 60% of the cycle and swings for 40%
    Pose walkPose(float f, float length, float stride, float dir)
    {
        float ph = fmodf(f, length) / length;
        Pose p = STANCE;
        auto foot = [&](float offset, Vector3 base, float& pitch) {
            float u = fmodf(ph + offset, 1.0f);
            Vector3 r = base;
            if (u < 0.6f) r.z += dir * stride * (0.5f - u / 0.6f);
            else
            {
                float s = (u - 0.6f) / 0.4f;
                r.z += dir * stride * (-0.5f + smooth01(s));
                r.y += 0.075f * sinf(s * PI);
                pitch += 16.0f * sinf(s * PI);
            }
            return r;
        };
        p.footL = foot(0.0f, STANCE.footL, p.footPitchL);
        p.footR = foot(0.5f, STANCE.footR, p.footPitchR);
        float a = ph * 2.0f * PI;
        p.hipsPos.y += -0.012f + 0.016f * cosf(a * 2.0f);
        p.hipsPos.x += 0.012f * sinf(a);
        p.hipsRot.y += 3.0f * sinf(a);
        p.chestRot.y -= 2.5f * sinf(a);
        p.chestRot.x += dir > 0 ? 3.0f : -3.0f;
        p.handL.y += 0.012f * sinf(a * 2.0f + 0.6f);
        p.handR.y += 0.012f * sinf(a * 2.0f + 1.4f);
        p.handL.z += 0.015f * sinf(a);
        return p;
    }

    Pose winPose(float f)
    {
        Pose p;
        float a = f / 50.0f * 2.0f * PI;
        float rise = smooth01(f / 22.0f);
        p.hipsPos = v3(0.0f, -0.01f + 0.012f * sinf(a), 0.0f);
        p.hipsRot = v3(0, -72, 0);
        p.chestRot = v3(-7, -80, 0);
        p.headRot = v3(-9, -66, 0);
        p.footL = v3(0.05f, 0.0f, 0.17f);  p.footYawL = -48; p.kneeL = v3(-0.7f, 0.0f, 0.7f);
        p.footR = v3(-0.06f, 0.0f, -0.17f); p.footYawR = -92; p.kneeR = v3(-1.0f, 0.0f, 0.0f);
        // raised fist (pumps) + other hand on the hip
        p.handR = v3(-0.24f, 1.80f + 0.07f * sinf(a * 2.0f), -0.26f); p.elbowR = v3(-0.2f, -0.4f, -1.0f);
        p.handL = v3(-0.03f, 0.97f, 0.31f); p.elbowL = v3(0.3f, 0.1f, 1.0f);
        return poseLerp(STANCE, p, rise);
    }

    Pose relaxedPose()
    {
        Pose p;
        p.hipsRot = v3(0, -22, 0);
        p.chestRot = v3(0, -24, 0);
        p.headRot = v3(0, -12, 0);
        p.handL = v3(0.26f, 0.86f, 0.10f);  p.elbowL = v3(0.4f, 0.0f, -1.0f);
        p.handR = v3(-0.27f, 0.86f, -0.08f); p.elbowR = v3(-0.4f, 0.0f, -1.0f);
        p.footL = v3(0.10f, 0.0f, 0.06f);   p.footYawL = -5;
        p.footR = v3(-0.11f, 0.0f, -0.05f); p.footYawR = -30; p.kneeR = v3(-0.5f, 0.0f, 0.9f);
        p.fistL = p.fistR = 0.35f;
        return p;
    }
}

// ---------------------------------------------------------------------------
void animInit()
{
    STANCE = makeStance();
    CROUCH = makeCrouch();
    AIR = makeAir();
    const Pose S = STANCE, C = CROUCH;

    addProc(ANIM_IDLE, "idle", 72, true, idlePose);
    addProc(ANIM_WALK_F, "walk_f", 26, true, [](float f) { return walkPose(f, 26, 0.40f, 1.0f); });
    addProc(ANIM_WALK_B, "walk_b", 30, true, [](float f) { return walkPose(f, 30, 0.36f, -1.0f); });
    addProc(ANIM_WIN, "win", 100000, false, winPose);

    // ---- crouch (frame 0 = standing, frame 6 = fully crouched) ----
    addTrack(ANIM_CROUCH, "crouch", { { 0, S, E_LIN }, { 6, C, E_OUT } });

    // ---- intro: relaxed -> fighting stance ----
    {
        Pose r = relaxedPose();
        Pose mid = poseLerp(r, S, 0.5f);
        mid.hipsPos.y -= 0.05f;
        addTrack(ANIM_INTRO, "intro", { { 0, r, E_LIN }, { 30, r, E_LIN }, { 44, mid, E_IN }, { 58, S, E_OUT } });
    }

    // ---- jump (40 frames = the whole air time, the fighter rescales it) ----
    {
        Pose take = S;
        take.hipsPos = v3(0, 0.02f, 0); take.hipsRot.x = 2; take.chestRot.x = 4;
        take.footL = v3(0.10f, -0.03f, 0.12f); take.footPitchL = 42;
        take.footR = v3(-0.13f, -0.03f, -0.14f); take.footPitchR = 42;
        take.handL = v3(0.20f, 1.24f, 0.30f); take.handR = v3(-0.24f, 1.18f, 0.10f);
        Pose tuck = AIR;
        Pose fall = AIR;
        fall.footL = v3(0.10f, 0.10f, 0.20f); fall.footPitchL = 30;
        fall.footR = v3(-0.13f, 0.06f, -0.16f); fall.footPitchR = 30;
        fall.hipsRot.x = 4; fall.chestRot.x = 10;
        fall.handL = v3(0.24f, 1.26f, 0.28f); fall.handR = v3(-0.28f, 1.20f, 0.06f);
        addTrack(ANIM_JUMP, "jump", { { 0, take, E_LIN }, { 14, tuck, E_OUT }, { 26, tuck, E_LIN }, { 40, fall, E_INOUT } });
    }

    // ---- blocking ----
    Pose block = S;
    {
        block.hipsPos = v3(0, -0.11f, -0.09f);
        block.hipsRot = v3(-1, -54, 0);
        block.chestRot = v3(12, -50, 0);
        block.headRot = v3(14, -14, 0);
        block.handL = v3(0.03f, 1.33f, 0.31f); block.elbowL = v3(0.25f, -1.0f, 0.1f);
        block.handR = v3(-0.09f, 1.31f, 0.21f); block.elbowR = v3(-0.30f, -1.0f, -0.1f);
        addTrack(ANIM_BLOCK, "block", { { 0, block, E_LIN }, { 1, block, E_LIN } });
    }
    Pose blockC = C;
    {
        blockC.hipsPos.z -= 0.04f;
        blockC.hipsRot = v3(12, -54, 0);
        blockC.chestRot = v3(30, -50, 0);
        blockC.headRot = v3(8, -14, 0);
        blockC.handL = v3(0.03f, 0.98f, 0.36f); blockC.elbowL = v3(0.25f, -1.0f, 0.1f);
        blockC.handR = v3(-0.09f, 0.95f, 0.27f); blockC.elbowR = v3(-0.30f, -1.0f, -0.1f);
        addTrack(ANIM_BLOCK_CROUCH, "block_crouch", { { 0, blockC, E_LIN }, { 1, blockC, E_LIN } });
    }

    // ---- hit reactions (30 frames, rescaled to the hit-stun length) ----
    {
        Pose h = S;                      // head snaps back
        h.hipsPos = v3(0, -0.10f, -0.12f);
        h.hipsRot = v3(-9, -38, 0);
        h.chestRot = v3(-24, -22, 4);
        h.headRot = v3(-36, 8, 6);
        h.handL = v3(0.22f, 1.08f, 0.26f); h.handR = v3(-0.30f, 1.00f, 0.02f);
        h.fistL = h.fistR = 0.5f;
        Pose h2 = poseLerp(h, S, 0.35f);
        addTrack(ANIM_HIT_HIGH, "hit_high", { { 0, S, E_LIN }, { 3, h, E_OUT }, { 14, h2, E_INOUT }, { 30, S, E_INOUT } });

        Pose g = S;                      // folds over a body blow
        g.hipsPos = v3(0, -0.16f, -0.17f);
        g.hipsRot = v3(20, -40, 0);
        g.chestRot = v3(50, -32, 0);
        g.headRot = v3(34, -10, 0);
        g.handL = v3(0.08f, 0.92f, 0.20f); g.handR = v3(-0.12f, 0.88f, 0.14f);
        g.kneeL = v3(0.3f, 0, 1);
        Pose g2 = poseLerp(g, S, 0.35f);
        addTrack(ANIM_HIT_LOW, "hit_low", { { 0, S, E_LIN }, { 3, g, E_OUT }, { 14, g2, E_INOUT }, { 30, S, E_INOUT } });

        Pose c = C;                      // hit while crouching
        c.hipsPos = v3(0, -0.43f, -0.16f);
        c.hipsRot = v3(2, -38, 0);
        c.chestRot = v3(6, -22, 4);
        c.headRot = v3(-28, 6, 5);
        c.handL = v3(0.20f, 0.86f, 0.28f); c.handR = v3(-0.26f, 0.80f, 0.06f);
        Pose c2 = poseLerp(c, C, 0.35f);
        addTrack(ANIM_HIT_CROUCH, "hit_crouch", { { 0, C, E_LIN }, { 3, c, E_OUT }, { 14, c2, E_INOUT }, { 30, C, E_INOUT } });
    }

    // ---- knockdown: flying back, lying on the ground, getting up ----
    Pose lying;
    {
        Pose fly = S;
        fly.hipsPos = v3(0, -0.22f, -0.05f);
        fly.hipsRot = v3(-52, -18, 0);
        fly.chestRot = v3(-66, -10, 0);
        fly.headRot = v3(-44, 0, 0);
        fly.handL = v3(0.42f, 1.02f, 0.14f); fly.elbowL = v3(1.0f, -0.3f, -0.3f);
        fly.handR = v3(-0.46f, 0.94f, 0.04f); fly.elbowR = v3(-1.0f, -0.3f, -0.3f);
        fly.footL = v3(0.13f, 0.42f, 0.52f); fly.kneeL = v3(0.2f, 1.0f, 0.4f); fly.footPitchL = 20; fly.footYawL = 0;
        fly.footR = v3(-0.14f, 0.28f, 0.44f); fly.kneeR = v3(-0.2f, 1.0f, 0.4f); fly.footPitchR = 20; fly.footYawR = 0;
        fly.fistL = fly.fistR = 0.3f;
        Pose fly2 = fly;
        fly2.hipsRot.x = -74; fly2.chestRot.x = -84; fly2.headRot.x = -62;
        fly2.hipsPos = v3(0, -0.40f, -0.05f);
        fly2.footL = v3(0.14f, 0.36f, 0.62f); fly2.footR = v3(-0.14f, 0.24f, 0.58f);
        fly2.handL = v3(0.46f, 0.80f, -0.12f); fly2.handR = v3(-0.48f, 0.74f, -0.18f);
        addTrack(ANIM_FALL, "fall", { { 0, S, E_LIN }, { 5, fly, E_OUT }, { 22, fly2, E_INOUT } });

        lying = S;
        lying.hipsPos = v3(0, -0.80f, 0.0f);          // hips ~13 cm above the ground
        lying.hipsRot = v3(-88, 0, 0);
        lying.chestRot = v3(-93, 0, 0);
        lying.headRot = v3(-84, 0, 0);
        lying.handL = v3(0.44f, 0.07f, -0.20f); lying.elbowL = v3(1.0f, 0.2f, 0.0f);
        lying.handR = v3(-0.44f, 0.07f, -0.26f); lying.elbowR = v3(-1.0f, 0.2f, 0.0f);
        lying.footL = v3(0.16f, -0.05f, 0.80f); lying.kneeL = v3(0.3f, 1.0f, 0.0f); lying.footPitchL = -62; lying.footYawL = 12;
        lying.footR = v3(-0.16f, -0.05f, 0.76f); lying.kneeR = v3(-0.3f, 1.0f, 0.0f); lying.footPitchR = -62; lying.footYawR = -12;
        lying.fistL = lying.fistR = 0.25f;
        addTrack(ANIM_DOWN, "down", { { 0, lying, E_LIN }, { 1, lying, E_LIN } });

        Pose sit = C;
        sit.hipsPos = v3(0, -0.66f, -0.02f);
        sit.hipsRot = v3(-28, -10, 0);
        sit.chestRot = v3(4, -10, 0);
        sit.headRot = v3(10, -6, 0);
        sit.handL = v3(0.32f, 0.10f, -0.26f); sit.elbowL = v3(0.6f, 0.2f, -1.0f);
        sit.handR = v3(-0.34f, 0.10f, -0.30f); sit.elbowR = v3(-0.6f, 0.2f, -1.0f);
        sit.footL = v3(0.15f, 0.0f, 0.40f); sit.kneeL = v3(0.2f, 1.0f, 0.3f); sit.footPitchL = 0; sit.footYawL = 0;
        sit.footR = v3(-0.16f, 0.0f, 0.30f); sit.kneeR = v3(-0.3f, 1.0f, 0.3f); sit.footPitchR = 0; sit.footYawR = -10;
        sit.fistL = sit.fistR = 0.2f;
        Pose kneel = C;
        kneel.hipsPos = v3(0, -0.46f, -0.04f);
        kneel.chestRot.x = 34;
        kneel.handL = v3(0.10f, 0.72f, 0.36f); kneel.handR = v3(-0.20f, 0.60f, 0.10f);
        addTrack(ANIM_GETUP, "getup", { { 0, lying, E_LIN }, { 11, sit, E_INOUT }, { 21, kneel, E_INOUT }, { 32, S, E_INOUT } });
    }

    // =====================================================================
    //  ATTACKS  (frame numbers must match the move table in fighter.cpp)
    // =====================================================================

    // ---- LP: jab with the lead hand ----
    {
        Pose e = S;
        e.hipsPos = v3(0, -0.09f, 0.04f);
        e.hipsRot = v3(7, -52, 0);
        e.chestRot = v3(13, -58, 0);
        e.handL = v3(0.00f, 1.36f, 0.92f); e.elbowL = v3(0.6f, -0.7f, 0.0f);
        e.handR = v3(-0.12f, 1.29f, 0.10f);
        addTrack(ANIM_LP, "lp", { { 0, S, E_LIN }, { 4, e, E_OUT }, { 7, e, E_LIN }, { 15, S, E_INOUT } });
    }

    // ---- HP: straight right (rear hand), full body rotation ----
    {
        Pose w = S;                       // wind up
        w.hipsRot = v3(4, -50, 0);
        w.chestRot = v3(10, -46, 0);
        w.handR = v3(-0.20f, 1.24f, 0.02f);
        w.hipsPos = v3(0, -0.10f, -0.04f);
        Pose e = S;
        e.hipsPos = v3(0.0f, -0.12f, 0.16f);
        e.hipsRot = v3(10, 8, 0);
        e.chestRot = v3(16, 34, 0);
        e.headRot = v3(4, 2, 0);
        e.handR = v3(0.02f, 1.36f, 0.98f); e.elbowR = v3(-0.6f, -0.6f, 0.0f);
        e.handL = v3(0.16f, 1.28f, 0.08f); e.elbowL = v3(0.7f, -1.0f, -0.4f);
        e.footR = v3(-0.13f, 0.06f, -0.24f); e.footPitchR = 42; e.footYawR = -12; e.kneeR = v3(-0.15f, 0, 1.0f);
        e.footL = v3(0.10f, 0.0f, 0.30f); e.kneeL = v3(0.2f, 0, 1.0f);
        Pose r = poseLerp(e, S, 0.5f);
        addTrack(ANIM_HP, "hp", { { 0, S, E_LIN }, { 4, w, E_OUT }, { 9, e, E_OUT }, { 13, e, E_LIN }, { 20, r, E_INOUT }, { 30, S, E_INOUT } });
    }

    // ---- LK: snap kick with the lead leg ----
    {
        Pose ch = S;                      // chamber: knee up
        ch.hipsPos = v3(-0.02f, -0.05f, -0.03f);
        ch.hipsRot = v3(-4, -40, 0);
        ch.chestRot = v3(4, -30, 0);
        ch.footL = v3(0.09f, 0.46f, 0.26f); ch.kneeL = v3(0.1f, 0.6f, 1.0f); ch.footPitchL = 30;
        Pose e = ch;
        e.hipsPos = v3(-0.02f, -0.04f, 0.02f);
        e.hipsRot = v3(-12, -40, 0);
        e.chestRot = v3(-6, -30, 0);
        e.footL = v3(0.06f, 0.70f, 0.95f); e.kneeL = v3(0.05f, 1.0f, 0.4f); e.footPitchL = 48;
        e.handL = v3(0.14f, 1.22f, 0.26f); e.handR = v3(-0.16f, 1.26f, 0.08f);
        addTrack(ANIM_LK, "lk", { { 0, S, E_LIN }, { 3, ch, E_OUT }, { 6, e, E_OUT }, { 9, e, E_LIN }, { 13, ch, E_INOUT }, { 22, S, E_INOUT } });
    }

    // ---- HK: roundhouse with the rear leg ----
    {
        Pose ch = S;                      // chamber: rear knee lifts out to the side
        ch.hipsPos = v3(0.02f, -0.04f, 0.04f);
        ch.hipsRot = v3(-6, -8, -8);
        ch.chestRot = v3(-4, -20, -6);
        ch.footL = v3(0.10f, 0.0f, 0.22f); ch.footYawL = -40; ch.kneeL = v3(-0.5f, 0, 1.0f);
        ch.footR = v3(-0.34f, 0.60f, 0.02f); ch.kneeR = v3(-1.0f, 0.5f, 0.6f); ch.footPitchR = 40; ch.footYawR = 0;
        ch.handL = v3(0.20f, 1.24f, 0.22f); ch.handR = v3(-0.22f, 1.22f, 0.10f);
        Pose e = ch;                      // extension: hips turned over, leg whips across
        e.hipsPos = v3(0.03f, -0.02f, 0.08f);
        e.hipsRot = v3(-22, 62, -10);
        e.chestRot = v3(-26, 30, -8);
        e.headRot = v3(0, 4, 0);
        e.footL = v3(0.10f, 0.02f, 0.22f); e.footYawL = -78; e.footPitchL = 10; e.kneeL = v3(-1.0f, 0, 0.5f);
        e.footR = v3(0.02f, 1.13f, 0.90f); e.kneeR = v3(-0.5f, 0.6f, 0.5f); e.footPitchR = 55; e.footYawR = 60;
        e.handL = v3(0.34f, 1.08f, -0.10f); e.elbowL = v3(1.0f, -0.4f, -0.6f);
        e.handR = v3(-0.26f, 1.16f, 0.16f); e.elbowR = v3(-1.0f, -0.6f, -0.2f);
        Pose rc = poseLerp(ch, S, 0.25f);
        addTrack(ANIM_HK, "hk", { { 0, S, E_LIN }, { 6, ch, E_OUT }, { 12, e, E_OUT }, { 16, e, E_LIN }, { 25, rc, E_INOUT }, { 38, S, E_INOUT } });
    }

    // ---- crouching LP: low jab ----
    {
        Pose e = C;
        e.hipsRot = v3(15, -52, 0);
        e.chestRot = v3(28, -58, 0);
        e.hipsPos.z += 0.04f;
        e.handL = v3(0.00f, 0.98f, 0.92f); e.elbowL = v3(0.6f, -0.7f, 0.0f);
        addTrack(ANIM_CLP, "clp", { { 0, C, E_LIN }, { 4, e, E_OUT }, { 7, e, E_LIN }, { 14, C, E_INOUT } });
    }

    // ---- crouching HP: rising uppercut (launcher) ----
    {
        Pose w = C;                       // sink + load the rear hand
        w.hipsPos = v3(0, -0.46f, -0.04f);
        w.hipsRot = v3(18, -50, 0);
        w.chestRot = v3(32, -52, 0);
        w.handR = v3(-0.18f, 0.60f, 0.12f); w.elbowR = v3(-0.5f, -0.2f, -1.0f);
        Pose e = S;                       // explode upward
        e.hipsPos = v3(0.0f, 0.02f, 0.12f);
        e.hipsRot = v3(-4, 6, 0);
        e.chestRot = v3(-12, 30, 0);
        e.headRot = v3(-12, 0, 0);
        e.handR = v3(0.00f, 1.80f, 0.46f); e.elbowR = v3(-0.3f, -1.0f, 0.4f);
        e.handL = v3(0.20f, 1.16f, 0.06f); e.elbowL = v3(0.8f, -0.8f, -0.5f);
        e.footL = v3(0.10f, 0.0f, 0.28f); e.kneeL = v3(0.15f, 0, 1.0f);
        e.footR = v3(-0.13f, 0.07f, -0.20f); e.footPitchR = 48; e.footYawR = -14; e.kneeR = v3(-0.2f, 0, 1.0f);
        Pose r = poseLerp(e, S, 0.55f);
        addTrack(ANIM_CHP, "chp", { { 0, C, E_LIN }, { 4, w, E_OUT }, { 8, e, E_OUT }, { 14, e, E_LIN }, { 22, r, E_INOUT }, { 34, S, E_INOUT } });
    }

    // ---- crouching LK: low kick ----
    {
        Pose e = C;
        e.hipsPos = v3(0, -0.43f, -0.14f);
        e.hipsRot = v3(2, -40, 0);
        e.chestRot = v3(20, -30, 0);
        e.footL = v3(0.07f, 0.05f, 0.96f); e.kneeL = v3(0.2f, 1.0f, 0.4f); e.footPitchL = 20;
        addTrack(ANIM_CLK, "clk", { { 0, C, E_LIN }, { 5, e, E_OUT }, { 8, e, E_LIN }, { 20, C, E_INOUT } });
    }

    // ---- crouching HK: sweep (knocks down) ----
    {
        Pose w = C;
        w.hipsPos = v3(0, -0.47f, -0.05f);
        w.hipsRot = v3(12, -20, 0);
        w.chestRot = v3(30, -12, 0);
        w.footR = v3(-0.36f, 0.06f, -0.06f); w.kneeR = v3(-1.0f, 0.4f, 0.3f); w.footPitchR = 10;
        w.handL = v3(0.16f, 0.60f, 0.28f); w.handR = v3(-0.14f, 0.80f, 0.16f);
        Pose e = w;
        e.hipsPos = v3(0.04f, -0.52f, -0.06f);
        e.hipsRot = v3(-6, 56, 0);
        e.chestRot = v3(22, 30, 0);
        e.headRot = v3(-6, 0, 0);
        e.footL = v3(0.10f, 0.03f, 0.10f); e.footYawL = -60; e.kneeL = v3(-0.6f, 0.2f, 1.0f); e.footPitchL = 20;
        e.footR = v3(0.02f, 0.03f, 1.02f); e.kneeR = v3(-0.6f, 1.0f, 0.2f); e.footPitchR = 30; e.footYawR = 40;
        e.handL = v3(0.26f, 0.34f, -0.16f); e.elbowL = v3(0.8f, 0.2f, -1.0f); e.fistL = 0.2f;
        e.handR = v3(-0.22f, 0.86f, 0.10f);
        addTrack(ANIM_CHK, "chk", { { 0, C, E_LIN }, { 5, w, E_OUT }, { 10, e, E_OUT }, { 14, e, E_LIN }, { 26, w, E_INOUT }, { 38, C, E_INOUT } });
    }

    // ---- jumping punch ----
    {
        Pose e = AIR;
        e.hipsRot = v3(22, 4, 0);
        e.chestRot = v3(34, 30, 0);
        e.headRot = v3(14, 0, 0);
        e.handR = v3(0.00f, 0.98f, 0.76f); e.elbowR = v3(-0.6f, 0.4f, -0.3f);
        e.handL = v3(0.22f, 1.20f, 0.02f);
        e.footL = v3(0.10f, 0.30f, 0.10f); e.footR = v3(-0.13f, 0.36f, -0.26f); e.kneeR = v3(-0.4f, -0.3f, 1.0f);
        addTrack(ANIM_JP, "jp", { { 0, AIR, E_LIN }, { 5, e, E_OUT }, { 60, e, E_LIN } });
    }

    // ---- jumping kick ----
    {
        Pose e = AIR;
        e.hipsRot = v3(-14, -40, 0);
        e.chestRot = v3(0, -30, 0);
        e.footL = v3(0.07f, 0.16f, 0.90f); e.kneeL = v3(0.1f, 1.0f, 0.3f); e.footPitchL = 40;
        e.footR = v3(-0.13f, 0.46f, -0.16f); e.kneeR = v3(-0.5f, 0.2f, 1.0f); e.footPitchR = 40;
        e.handL = v3(0.20f, 1.22f, 0.20f); e.handR = v3(-0.26f, 1.16f, -0.02f);
        addTrack(ANIM_JK, "jk", { { 0, AIR, E_LIN }, { 6, e, E_OUT }, { 60, e, E_LIN } });
    }

    // ---- forward + HP: overhead smash (must be blocked standing) ----
    {
        Pose w = S;
        w.hipsPos = v3(0, -0.03f, -0.04f);
        w.hipsRot = v3(-6, -30, 0);
        w.chestRot = v3(-12, -14, 0);
        w.headRot = v3(-4, -6, 0);
        w.handR = v3(-0.12f, 1.88f, -0.04f); w.elbowR = v3(-0.5f, 0.2f, -1.0f);
        w.handL = v3(0.10f, 1.26f, 0.36f);
        Pose e = S;
        e.hipsPos = v3(0, -0.17f, 0.12f);
        e.hipsRot = v3(16, 6, 0);
        e.chestRot = v3(38, 30, 0);
        e.headRot = v3(14, 0, 0);
        e.handR = v3(0.02f, 1.02f, 0.88f); e.elbowR = v3(-0.5f, 0.8f, -0.4f);
        e.handL = v3(0.18f, 1.10f, 0.02f); e.elbowL = v3(0.8f, -0.6f, -0.6f);
        e.footR = v3(-0.13f, 0.05f, -0.22f); e.footPitchR = 38; e.footYawR = -14; e.kneeR = v3(-0.2f, 0, 1.0f);
        e.kneeL = v3(0.25f, 0, 1.0f);
        Pose r = poseLerp(e, S, 0.5f);
        addTrack(ANIM_FHP, "fhp", { { 0, S, E_LIN }, { 11, w, E_OUT }, { 14, w, E_LIN }, { 18, e, E_IN }, { 23, e, E_LIN }, { 30, r, E_INOUT }, { 40, S, E_INOUT } });
    }

    // ---- forward + HK: thrusting push kick with the rear leg ----
    {
        Pose ch = S;
        ch.hipsPos = v3(0.0f, -0.03f, 0.02f);
        ch.hipsRot = v3(-8, -6, 0);
        ch.chestRot = v3(-2, -12, 0);
        ch.footL = v3(0.10f, 0.0f, 0.20f); ch.footYawL = -30; ch.kneeL = v3(-0.3f, 0, 1.0f);
        ch.footR = v3(-0.10f, 0.58f, 0.16f); ch.kneeR = v3(-0.15f, 0.8f, 1.0f); ch.footPitchR = -10; ch.footYawR = 0;
        ch.handL = v3(0.16f, 1.26f, 0.26f); ch.handR = v3(-0.20f, 1.24f, 0.14f);
        Pose e = ch;
        e.hipsPos = v3(0.0f, -0.02f, 0.10f);
        e.hipsRot = v3(-24, 4, 0);
        e.chestRot = v3(-18, -4, 0);
        e.headRot = v3(2, -4, 0);
        e.footR = v3(-0.04f, 0.80f, 1.04f); e.kneeR = v3(-0.1f, 1.0f, 0.2f); e.footPitchR = -22;
        e.handL = v3(0.26f, 1.16f, 0.06f); e.handR = v3(-0.28f, 1.12f, 0.00f);
        Pose rc = poseLerp(ch, S, 0.3f);
        addTrack(ANIM_FHK, "fhk", { { 0, S, E_LIN }, { 6, ch, E_OUT }, { 11, e, E_OUT }, { 15, e, E_LIN }, { 23, rc, E_INOUT }, { 36, S, E_INOUT } });
    }

    // ---- dive kick (air special) ----
    {
        Pose e = AIR;
        e.hipsRot = v3(-30, -20, 0);
        e.chestRot = v3(-22, -14, 0);
        e.headRot = v3(6, -6, 0);
        e.footL = v3(0.06f, 0.04f, 0.86f); e.kneeL = v3(0.1f, 1.0f, 0.3f); e.footPitchL = 48;
        e.footR = v3(-0.14f, 0.52f, 0.02f); e.kneeR = v3(-0.5f, 0.3f, 1.0f); e.footPitchR = 40;
        e.handL = v3(0.34f, 1.24f, -0.10f); e.elbowL = v3(1.0f, -0.2f, -0.5f);
        e.handR = v3(-0.36f, 1.20f, -0.16f); e.elbowR = v3(-1.0f, -0.2f, -0.5f);
        addTrack(ANIM_DIVE, "dive", { { 0, AIR, E_LIN }, { 5, e, E_OUT }, { 90, e, E_LIN } });
    }

    // ---- SPECIAL: energy shot (projectile leaves the hands on frame 13) ----
    {
        Pose ch = S;                      // gather the energy at the rear hip
        ch.hipsPos = v3(0, -0.15f, -0.05f);
        ch.hipsRot = v3(4, -58, 0);
        ch.chestRot = v3(10, -72, 0);
        ch.headRot = v3(2, -14, 0);
        ch.handR = v3(-0.24f, 1.00f, -0.06f); ch.elbowR = v3(-0.6f, -0.2f, -1.0f);
        ch.handL = v3(-0.10f, 1.04f, 0.04f);  ch.elbowL = v3(0.8f, -0.8f, 0.2f);
        ch.fistL = ch.fistR = 0.15f;
        Pose e = S;                       // both palms thrust forward
        e.hipsPos = v3(0, -0.17f, 0.12f);
        e.hipsRot = v3(10, -16, 0);
        e.chestRot = v3(18, -4, 0);
        e.headRot = v3(4, 0, 0);
        e.handL = v3(0.07f, 1.24f, 0.86f); e.elbowL = v3(0.9f, -0.5f, 0.0f);
        e.handR = v3(-0.07f, 1.14f, 0.84f); e.elbowR = v3(-0.9f, -0.5f, 0.0f);
        e.fistL = e.fistR = 0.0f;
        e.kneeL = v3(0.25f, 0, 1.0f);
        e.footR = v3(-0.14f, 0.04f, -0.32f); e.footPitchR = 24;
        Pose r = poseLerp(e, S, 0.5f);
        addTrack(ANIM_FIREBALL, "fireball", { { 0, S, E_LIN }, { 8, ch, E_OUT }, { 13, e, E_OUT }, { 25, e, E_LIN }, { 32, r, E_INOUT }, { 42, S, E_INOUT } });

        // ---- SUPER: beam. Long charge (the game freezes around it), then a sustained blast ----
        Pose ch2 = ch;
        ch2.hipsPos = v3(0, -0.22f, -0.08f);
        ch2.hipsRot = v3(6, -64, 0);
        ch2.chestRot = v3(14, -84, 0);
        ch2.headRot = v3(4, -18, 0);
        ch2.handR = v3(-0.28f, 0.96f, -0.16f);
        ch2.handL = v3(-0.14f, 1.00f, -0.08f);
        ch2.footL = v3(0.10f, 0.0f, 0.32f); ch2.footR = v3(-0.14f, 0.03f, -0.34f);
        Pose e2 = e;
        e2.hipsPos = v3(0, -0.20f, 0.10f);
        e2.chestRot = v3(20, 0, 0);
        e2.hipsRot = v3(12, -12, 0);
        e2.footL = v3(0.10f, 0.0f, 0.36f); e2.footR = v3(-0.14f, 0.04f, -0.40f);
        std::vector<Key> beamKeys = { { 0, S, E_LIN }, { 10, ch2, E_OUT }, { 42, ch2, E_LIN }, { 46, e2, E_OUT }, { 102, e2, E_LIN }, { 112, r, E_INOUT }, { 126, S, E_INOUT } };
        addProc(ANIM_BEAM, "beam", 126, false, [beamKeys](float f) {
            Pose p = evalKeys(beamKeys, f);
            if (f > 8 && f < 44)           // trembling while charging
            {
                float k = (f - 8) / 36.0f;
                p.handR.y += 0.012f * k * sinf(f * 2.9f); p.handL.y += 0.012f * k * sinf(f * 2.9f + 1.0f);
                p.hipsPos.y -= 0.03f * k;
            }
            if (f >= 46 && f < 102)        // recoil shake while firing
            {
                p.handL.y += 0.014f * sinf(f * 3.7f); p.handR.y += 0.014f * sinf(f * 3.7f + 2.0f);
                p.handL.z += 0.012f * sinf(f * 5.1f); p.handR.z += 0.012f * sinf(f * 5.1f + 1.0f);
                p.hipsPos.z -= 0.02f + 0.01f * sinf(f * 4.3f);
                p.chestRot.x += 1.5f * sinf(f * 3.1f);
            }
            return p; });
    }

    // ---- SPECIAL (forward): dashing straight punch ----
    {
        Pose w = S;
        w.hipsPos = v3(0, -0.20f, -0.06f);
        w.hipsRot = v3(14, -52, 0);
        w.chestRot = v3(22, -56, 0);
        w.handR = v3(-0.26f, 1.02f, -0.14f); w.elbowR = v3(-0.5f, -0.2f, -1.0f);
        w.handL = v3(0.08f, 1.20f, 0.44f);
        Pose e = S;
        e.hipsPos = v3(0.0f, -0.24f, 0.26f);
        e.hipsRot = v3(22, 14, 0);
        e.chestRot = v3(32, 44, 0);
        e.headRot = v3(8, 4, 0);
        e.handR = v3(0.02f, 1.20f, 1.14f); e.elbowR = v3(-0.6f, -0.5f, 0.0f);
        e.handL = v3(0.22f, 1.12f, 0.04f); e.elbowL = v3(0.8f, -0.6f, -0.6f);
        e.footL = v3(0.10f, 0.0f, 0.46f); e.kneeL = v3(0.2f, 0, 1.0f);
        e.footR = v3(-0.13f, 0.07f, -0.50f); e.footPitchR = 50; e.footYawR = -10; e.kneeR = v3(-0.15f, 0, 1.0f);
        Pose r = poseLerp(e, S, 0.5f);
        addTrack(ANIM_RUSH, "rush", { { 0, S, E_LIN }, { 5, w, E_OUT }, { 12, e, E_OUT }, { 20, e, E_LIN }, { 30, r, E_INOUT }, { 44, S, E_INOUT } });
    }

    // ---- SPECIAL (down): rising uppercut that leaves the ground ----
    {
        Pose w = C;
        w.hipsPos = v3(0, -0.47f, -0.04f);
        w.hipsRot = v3(18, -50, 0);
        w.chestRot = v3(32, -52, 0);
        w.handR = v3(-0.18f, 0.60f, 0.12f); w.elbowR = v3(-0.5f, -0.2f, -1.0f);
        Pose e = S;
        e.hipsPos = v3(0.0f, 0.03f, 0.08f);
        e.hipsRot = v3(-8, 24, 0);
        e.chestRot = v3(-16, 48, 0);
        e.headRot = v3(-16, 6, 0);
        e.handR = v3(0.02f, 1.84f, 0.40f); e.elbowR = v3(-0.3f, -1.0f, 0.4f);
        e.handL = v3(0.24f, 1.10f, -0.04f); e.elbowL = v3(0.8f, -0.8f, -0.5f);
        e.footL = v3(0.10f, 0.42f, 0.34f); e.kneeL = v3(0.15f, 0.6f, 1.0f); e.footPitchL = 40;   // knee drives up
        e.footR = v3(-0.12f, -0.02f, -0.10f); e.footPitchR = 50; e.footYawR = -14; e.kneeR = v3(-0.2f, 0, 1.0f);
        Pose d = AIR;
        d.handR = v3(-0.16f, 1.46f, 0.28f); d.handL = v3(0.22f, 1.14f, 0.10f);
        d.hipsRot = v3(0, -10, 0); d.chestRot = v3(4, 6, 0);
        d.footL = v3(0.10f, 0.14f, 0.20f); d.footR = v3(-0.13f, 0.06f, -0.14f);
        Pose land = poseLerp(S, C, 0.7f);
        addTrack(ANIM_RISE, "rise", { { 0, C, E_LIN }, { 3, w, E_OUT }, { 7, e, E_OUT }, { 22, e, E_LIN }, { 36, d, E_INOUT }, { 42, land, E_OUT }, { 54, S, E_INOUT } });
    }
}

Pose animEval(AnimId id, float frame)
{
    const Track& t = tracks[id];
    if (t.proc) return t.proc(frame);
    if (t.loop && t.length > 0.0f) frame = fmodf(frame, t.length);
    return evalKeys(t.keys, frame);
}

float animLength(AnimId id) { return tracks[id].length; }
bool animLoops(AnimId id) { return tracks[id].loop; }
const char* animName(AnimId id) { return tracks[id].name; }
