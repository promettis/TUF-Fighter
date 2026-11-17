// ============================================================================
//  TUF FIGHTER - engine.h
//  Common includes, small math helpers and shared constants.
// ============================================================================
#pragma once

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Simulation constants
// ---------------------------------------------------------------------------
constexpr int   TICK_RATE = 60;                 // game logic runs at a fixed 60 ticks/second
constexpr float TICK_DT   = 1.0f / TICK_RATE;

// ---------------------------------------------------------------------------
// Small math helpers
// ---------------------------------------------------------------------------
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth01(float t) { t = saturate(t); return t * t * (3.0f - 2.0f * t); }
inline float easeOutCubic(float t) { t = 1.0f - saturate(t); return 1.0f - t * t * t; }
inline float easeInCubic(float t) { t = saturate(t); return t * t * t; }
inline float easeOutBack(float t)
{
    t = saturate(t);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}
// Frame-rate independent exponential approach (used by cameras, UI, ...)
inline float approach(float cur, float target, float speed, float dt)
{
    return target + (cur - target) * expf(-speed * dt);
}
inline Vector3 v3(float x, float y, float z) { return Vector3{ x, y, z }; }
inline Vector3 lerp3(Vector3 a, Vector3 b, float t) { return Vector3{ lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t) }; }

inline Quaternion quatIdentity() { return Quaternion{ 0, 0, 0, 1 }; }
// Rotation of `deg` degrees around a (unit) axis
inline Quaternion quatAxis(Vector3 axis, float deg) { return QuaternionFromAxisAngle(axis, deg * DEG2RAD); }
inline Vector3 rotate(Quaternion q, Vector3 v) { return Vector3RotateByQuaternion(v, q); }
// q = a * b   (b is applied first, then a)
inline Quaternion qmul(Quaternion a, Quaternion b) { return QuaternionMultiply(a, b); }

// Build a quaternion from an orthonormal basis (columns x, y, z)
inline Quaternion quatFromBasis(Vector3 x, Vector3 y, Vector3 z)
{
    Matrix m = MatrixIdentity();
    m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
    m.m4 = y.x; m.m5 = y.y; m.m6 = y.z;
    m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
    return QuaternionNormalize(QuaternionFromMatrix(m));
}

inline Color colorLerp(Color a, Color b, float t)
{
    t = saturate(t);
    return Color{ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                  (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}
inline Color withAlpha(Color c, float a) { c.a = (unsigned char)(saturate(a) * 255.0f); return c; }

// Deterministic tiny random generator (xorshift) - used by particles and the CPU player
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (next() & 0xFFFFFF) / 16777216.0f; }              // [0,1)
    float range(float a, float b) { return a + (b - a) * unit(); }
    bool chance(float p) { return unit() < p; }
    int pick(int n) { return (int)(next() % (uint32_t)n); }
};
