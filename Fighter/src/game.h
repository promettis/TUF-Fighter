// ============================================================================
//  TUF FIGHTER - game.h
//  A match between two fighters: rounds, hit detection, CPU opponent,
//  camera, effects and the in-fight HUD.
// ============================================================================
#pragma once
#include "engine.h"
#include "fighter.h"
#include "render.h"
#include "ui.h"

// A stage: a background model placed behind the fighting court
struct StageDef
{
    std::string name;
    std::string file;
    ModelData data;
    GpuModel  gpu;
    Matrix    world = MatrixIdentity();
    bool      loading = false, loaded = false;
    void place();                 // computes `world` from the model bounds

    // --- stages that are built in code instead of loaded from a model (stage_bloc.cpp) ---
    struct Part { Mesh mesh{}; int mat = 3; Color tint = WHITE; };
    bool      procedural = false;
    std::vector<Part> parts;
    Texture2D sign{};             // lettering on the facade
    Vector4   signRect{};
    float     glassTop = 0.0f;
    bool      hasEnv = false;
    Environment env;              // lighting / sky that belongs to this stage
    void buildUniversityBloc(const std::string& fontFile);
    void draw(Renderer& r);
    void unload();
};

// ---------------------------------------------------------------------------
// CPU controlled player
// ---------------------------------------------------------------------------
struct CpuBrain
{
    int difficulty = 1;           // 0 easy, 1 normal, 2 hard
    int timer = 0;
    int action = 0;
    std::vector<MoveId> plan;     // attack chain it wants to perform
    int lastSerial = -1;
    bool willBlock = false;
    bool oppWasAir = false, wantAntiAir = false;
    int  shotSeen = -1;           // id of the last projectile it reacted to
    int  shotReaction = 0;        // 0 ignore, 1 block, 2 jump

    void reset(int diff);
    InputState think(const Fighter& me, const Fighter& opp, Rng& rng, float incomingShot, int shotId);
};

// A flying energy shot
struct Projectile
{
    int   id = 0;
    int   owner = 0;
    float x = 0, y = 0, vx = 0;
    float radius = 0.24f;
    int   age = 0;
    int   damage = 80;
    Color a = WHITE, b = WHITE;
    bool  dead = false;
};

// ---------------------------------------------------------------------------
enum MatchPhase { MP_INTRO, MP_FIGHT, MP_KO, MP_WINPOSE, MP_OVER };

struct Particle
{
    Vector3 pos{}, vel{};
    float life = 0, maxLife = 1, size = 1, rot = 0;
    Color col = WHITE;
    int kind = 0;                 // 0 spark, 1 ring, 2 dust, 3 star flash, 4 glow mote, 5 speed line, 6 ground ring
};

struct TrailPoint { Vector3 p{}; float age = 0; };
struct BeamVisual { bool on = false; float x0 = 0, x1 = 0, y = 0; bool touching = false; float power = 0; Color a = WHITE, b = WHITE; int dir = 1; };

class Match
{
public:
    Fighter f[2];
    bool cpu[2] = { false, true };
    int  difficulty = 1;
    int  wins[2] = { 0, 0 };
    int  round = 1;
    int  roundsToWin = 2;
    float timeLeft = 99.0f;
    MatchPhase phase = MP_INTRO;
    int  phaseFrame = 0;
    int  roundWinner = -1;        // 0 / 1, 2 = draw
    int  matchWinner = -1;
    bool timeUp = false;
    bool debugBoxes = false;
    StageDef* stage = nullptr;

    void start(CharacterDef* a, CharacterDef* b, bool cpuA, bool cpuB, int diff, StageDef* st);
    void tick(const InputState in[2]);          // one fixed 60 Hz step
    void frame(float dt);                       // once per rendered frame (camera, particles, HUD easing)
    void render(Renderer& r);                   // 3D scene
    void drawOverlay(UI& ui);                   // sparks + HUD (2D)
    bool over() const { return phase == MP_OVER; }

    Camera3D camera{};

private:
    CpuBrain brain[2];
    Rng rng;
    int  hitstop = 0;
    int  hitVictim = -1;
    // camera
    Vector3 camPos{}, camTarget{};
    float camShake = 0;
    bool camSnap = true;
    // HUD
    float shownHealth[2] = { 1, 1 }, ghostHealth[2] = { 1, 1 };
    float ghostDelay[2] = { 0, 0 };
    int   comboCount[2] = { 0, 0 }, comboDamage[2] = { 0, 0 };
    float comboTimer[2] = { 0, 0 }, comboPop[2] = { 0, 0 };
    std::string annText, annSub;
    float annTime = 0, annDur = 0;
    Color annColor = WHITE;
    float hudTime = 0;
    float koFlash = 0;
    std::vector<Particle> parts;
    std::vector<Projectile> shots;
    int   nextShotId = 1;
    std::vector<TrailPoint> trail[2];
    Color trailColor[2] = { WHITE, WHITE };
    BeamVisual beam[2];
    int   superFreeze = 0, superOwner = -1;
    float superBanner = 99.0f;
    float dim = 1.0f;             // background brightness (darkens during supers)
    float fovKick = 0.0f;
    float whiteFlash = 0.0f;

    void startRound();
    void updateShots();
    void updateBeam(int who);
    void spawnShot(int who);
    void spawnCharge(Vector3 at, Color a, Color b, float radius, int count);
    void spawnBurst(Vector3 at, Color a, Color b, float power);
    void addMeter(int attacker, int dmg, bool blocked);
    void drawMeter(UI& ui, int side);
    void drawSuperBanner(UI& ui);
    void resolvePush();
    void resolveHits();
    void handleEvents();
    void announce(const std::string& text, const std::string& sub, float dur, Color col);
    void spawnHit(Vector3 at, bool heavy, bool blocked, int dir, Color tint);
    void spawnDust(Vector3 at, int count);
    void updateCamera(float dt);
    void drawHealthBar(UI& ui, int side);
};
