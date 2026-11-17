// ============================================================================
//  TUF FIGHTER - render.h
//  3D renderer: lit + GPU-skinned shader, shadow map, procedural sky/ground.
// ============================================================================
#pragma once
#include "engine.h"
#include "model.h"

struct Environment
{
    Vector3 sunDir = Vector3Normalize(v3(-0.45f, 0.62f, 0.64f)); // direction towards the sun
    Vector3 sunColor = v3(1.35f, 1.20f, 1.00f);
    Vector3 skyColor = v3(0.42f, 0.52f, 0.70f);                  // ambient light from above
    Vector3 groundColor = v3(0.30f, 0.26f, 0.22f);               // ambient light bounced from below
    Vector3 horizonColor = v3(0.74f, 0.80f, 0.88f);
    Vector3 zenithColor = v3(0.16f, 0.36f, 0.72f);
    float fogDensity = 0.0065f;
    float exposure = 1.0f;
    float time = 0.0f;
};

struct SurfaceParams
{
    Color tint = WHITE;
    float lit = 1.0f;                 // 1 = fully dynamic lighting, 0 = texture already contains lighting
    Vector4 flash{ 1, 1, 1, 0 };      // rgb + amount (hit flash)
    Vector4 rim{ 1, 1, 1, 0 };        // rgb + strength (edge light)
    bool doubleSided = false;
    float saturation = 1.0f;
};

class Renderer
{
public:
    Environment env;

    bool init();
    void shutdown();

    // --- shadow pass (call before beginScene) ---
    void beginShadows(Vector3 focus, float radius);
    void shadowSkinned(const GpuModel& m, const std::vector<Matrix>& bones, Matrix world);
    void shadowStatic(const GpuModel& m, Matrix world);
    void endShadows();
    void clearShadows();              // no shadow casters this frame

    // --- main pass ---
    void beginScene(Camera3D cam, bool drawSky = true);
    void drawSkinned(const GpuModel& m, const std::vector<Matrix>& bones, Matrix world, const SurfaceParams& sp);
    void drawStatic(const GpuModel& m, Matrix world, const SurfaceParams& sp);
    void drawGround(float arenaHalfWidth, bool campus = false);   // campus = lawns, drive and forecourt of the university stage
    void endScene();
    // procedurally shaded stage geometry (see stage_bloc.cpp). `mat`: 0 wall panels, 1 glass, 2 granite, 5 foliage, 6 brick, 7 windowed facade, other = plain colour
    void drawProc(const Mesh& mesh, int mat, Color tint, Texture2D sign, Vector4 signRect, float glassTop, bool doubleSided);
    void setExposure(float e);        // change the exposure in the middle of a scene (used to dim the background)

    // animated "electronic" backdrop with a neon grid floor (menus). Draw before beginScene(cam, false).
    void drawTechBackdrop(Camera3D cam, Vector3 tintLeft, Vector3 tintRight, Vector4 padA, Vector4 padB, float time);

    Camera3D camera{};
    bool shadowsEnabled = true;

private:
    Shader lit{}, depth{}, ground{}, sky{}, tech{}, bloc{};
    Material matBloc{};
    int bMat = -1, bSign = -1, bZenith = -1, bGlassTop = -1, bTime = -1;
    struct TechLocs { int camPos, camF, camR, camU, tanHalf, aspect, res, time, tintL, tintR, padA, padB; } T{};
    Material matLit{}, matDepth{}, matGround{};
    Mesh groundMesh{};
    RenderTexture2D shadowMap{};
    Matrix lightVP = MatrixIdentity();
    bool shadowsValid = false;
    Texture2D white{};

    struct LitLocs
    {
        int viewPos, sunDir, sunColor, skyColor, groundColor, fogColor, fogDensity, exposure;
        int lightVP, shadowMap, skinned, bones, flash, rim, lit, useNormal, useOrm, saturation, shadowOn;
    } L{}, G{}, B{};
    int gArena = -1, gTime = -1;
    int dSkinned = -1, dBones = -1;
    struct SkyLocs { int camPos, camF, camR, camU, tanHalf, aspect, res, sunDir, sunColor, horizon, zenith, time, exposure; } S{};

    void fetchLocs(Shader& sh, LitLocs& l);
    void applyCommon(Shader& sh, LitLocs& l);
};
