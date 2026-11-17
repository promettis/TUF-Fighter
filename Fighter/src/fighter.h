// ============================================================================
//  TUF FIGHTER - fighter.h
//  One fighter: state machine, move table (frame data), hit / hurt boxes.
// ============================================================================
#pragma once
#include "engine.h"
#include "model.h"
#include "pose.h"
#include "anims.h"

// ---------------------------------------------------------------------------
// A playable character (model + stats)
// ---------------------------------------------------------------------------
struct CharacterDef
{
    std::string name;            // display name (upper case)
    std::string file;            // path of the .glb
    std::string title;           // short description shown on the select screen
    float power = 1.0f;          // damage multiplier
    float speed = 1.0f;          // walk speed multiplier
    int   health = 1000;
    int   statPower = 3, statSpeed = 3, statDefense = 3;   // 1..5 bars on the select screen
    Color accent = WHITE;
    // special move flavour
    std::string shotName = "ENERGY SHOT", superName = "POWER BEAM";
    Color fxA = { 255, 150, 50, 255 };   // outer colour of the energy effects
    Color fxB = { 255, 235, 150, 255 };  // inner colour
    float shotSpeed = 8.0f;
    int   shotDamage = 80;
    Texture2D photo{};                   // portrait photo for the menus (optional)

    ModelData data;
    GpuModel  gpu;
    Rig       rig;
    RenderTexture2D portrait{};
    bool usable = false;
};

// ---------------------------------------------------------------------------
// Input of one player for one tick (already translated to "buttons")
// ---------------------------------------------------------------------------
struct InputState
{
    bool left = false, right = false, up = false, down = false;
    bool lp = false, hp = false, lk = false, hk = false;   // pressed this tick
    bool sp = false, super = false;                        // special / super buttons (pressed this tick)
};

// ---------------------------------------------------------------------------
// Moves
// ---------------------------------------------------------------------------
enum MoveId
{
    MV_NONE = -1,
    MV_LP, MV_HP, MV_LK, MV_HK, MV_CLP, MV_CHP, MV_CLK, MV_CHK, MV_JP, MV_JK,
    MV_FHP, MV_FHK, MV_DIVE,
    MV_FIREBALL, MV_RUSH, MV_RISE, MV_BEAM,
    MV_COUNT
};
enum HitLevel { HL_MID, HL_LOW, HL_OVERHEAD };          // LOW must be blocked crouching, OVERHEAD standing
enum Limb { LIMB_LEAD_HAND, LIMB_REAR_HAND, LIMB_LEAD_FOOT, LIMB_REAR_FOOT };
enum Button { BTN_NONE = -1, BTN_LP, BTN_HP, BTN_LK, BTN_HK, BTN_SP, BTN_SUPER };

struct MoveDef
{
    const char* name;
    AnimId anim;
    int startup;        // frames before the attack can hit
    int active;         // frames the hit box is out
    int total;          // total length of the move
    int damage;
    int hitstun;        // frames the victim is stunned on hit
    int blockstun;      // ... on block
    float pushHit;      // knock back speed (m/s) on hit / on block
    float pushBlock;
    HitLevel level;
    Limb limb;          // which limb carries the hit box
    float radius;       // hit box padding around the limb
    bool knockdown;     // puts a grounded victim on the floor
    float launchVx, launchVy; // velocity given to a knocked down / airborne victim
    int hitstop;        // freeze frames on contact
    float lunge;        // forward speed while starting up (m/s)
    int rank;           // cancel rank: a move can be cancelled into a higher rank on contact
    bool heavy;
    float chip;         // fraction of the damage dealt through a block
    bool noScale;       // ignores combo damage scaling (super)
    bool special;
};
const MoveDef& moveDef(MoveId id);
const MoveDef& projectileHitDef();      // hit properties of the energy shot
const MoveDef& beamHitDef(bool last);   // hit properties of one beam tick
constexpr int BEAM_TICK = 6;            // the beam hits every 6 frames
constexpr float SUPER_COST = 100.0f;

// ---------------------------------------------------------------------------
enum FighterState
{
    FS_INTRO, FS_IDLE, FS_WALK_F, FS_WALK_B, FS_CROUCH, FS_PREJUMP, FS_JUMP, FS_LAND,
    FS_ATTACK, FS_BLOCK, FS_HIT, FS_FALL, FS_DOWN, FS_GETUP, FS_KO, FS_WIN
};

struct Box
{
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool overlaps(const Box& o) const { return x0 < o.x1 && x1 > o.x0 && y0 < o.y1 && y1 > o.y0; }
    Vector2 center() const { return { (x0 + x1) * 0.5f, (y0 + y1) * 0.5f }; }
};

enum HitResult { HIT_NONE, HIT_CONNECT, HIT_BLOCKED };
enum FighterEvent
{
    EV_WHOOSH_LIGHT = 1, EV_WHOOSH_HEAVY = 2, EV_JUMP = 4, EV_LAND = 8, EV_BODY_FALL = 16,
    EV_SHOT = 32,          // release the projectile now
    EV_SPECIAL = 64,       // a special move started
    EV_SUPER = 128,        // the super started (the match freezes around it)
    EV_BEAM = 256          // the beam starts firing
};

// Tuning constants shared with the CPU player
constexpr float ARENA_HALF = 6.2f;       // the fighters can move within +-ARENA_HALF
constexpr float MAX_SEPARATION = 5.6f;
constexpr float GRAVITY = 24.0f;
constexpr float JUMP_SPEED = 7.6f;
constexpr float JUMP_DRIFT = 3.1f;
constexpr float WALK_FWD = 2.35f;
constexpr float WALK_BACK = 1.85f;
constexpr float PUSH_HALF = 0.36f;

class Fighter
{
public:
    CharacterDef* ch = nullptr;
    int   index = 0;

    // --- simulation state ---
    float x = 0, y = 0, vx = 0, vy = 0, kbx = 0;
    int   facing = 1;                       // +1 looks to the right (+X), -1 to the left
    FighterState state = FS_IDLE;
    int   stateFrame = 0;
    MoveId move = MV_NONE;
    int   moveFrame = 0;
    bool  moveConnected = false;            // the current move touched the opponent (hit or block)
    bool  moveHasHit = false;               // the hit box was already consumed
    bool  airAttackUsed = false;
    int   attackSerial = 0;                 // increases with every attack started (used by the CPU player)
    int   jumpDir = 0;
    int   health = 1000, maxHealth = 1000;
    int   stunLeft = 0, stunTotal = 1;
    AnimId hitAnim = ANIM_HIT_HIGH;
    bool  blockCrouch = false;
    bool  bounced = false;
    int   comboTaken = 0;                   // hits received in the current combo
    int   comboDamage = 0;
    int   juggle = 0;
    float meter = 0.0f;                     // super meter 0..100
    bool  shotOut = false;                  // own projectile is still flying (set by the match)
    bool  dangerNear = false;               // a projectile / beam is about to arrive (set by the match)
    int   landFrame = -1;
    Button buffered = BTN_NONE;
    int   bufferTime = 0;
    InputState in;
    unsigned events = 0;                    // FighterEvent bits raised this tick (sounds / effects)

    // --- presentation ---
    Pose  pose, fromPose;
    int   blendLeft = 0, blendTotal = 1;
    float animTime = 0;
    float visualYaw = 90.0f;
    float flash = 0.0f;                     // white hit flash 0..1
    float shake = 0.0f;                     // shiver while in hit stop
    std::vector<BonePose> bones;
    std::vector<Matrix> skin;

    // --- collision (2D, in the fight plane) ---
    std::vector<Box> hurt;
    Box   hit;
    bool  hitActive = false;

    void reset(CharacterDef* c, int idx, float startX, int face);
    void update(const InputState& input, Fighter& opp);      // one logic tick
    void bufferInput(const InputState& input);               // remember attack presses (also during hit stop)
    void updatePose();                                       // animation + boxes
    HitResult receiveHit(const MoveDef& m, Fighter& attacker, Vector2 at);
    void setState(FighterState s, int blend = 5);
    void startWin();
    void forceKO();

    bool grounded() const { return y <= 0.0001f && state != FS_JUMP && state != FS_FALL; }
    bool airborne() const { return state == FS_JUMP || state == FS_FALL || y > 0.0001f; }
    bool attacking() const { return state == FS_ATTACK || (state == FS_JUMP && move != MV_NONE); }
    bool isFree() const { return state == FS_IDLE || state == FS_WALK_F || state == FS_WALK_B || state == FS_CROUCH; }
    bool invulnerable() const;
    bool beamActive() const;                // the super beam is currently firing
    Vector3 handsWorld() const;             // point between the two hands
    Vector3 limbTipWorld() const;           // tip of the limb used by the current attack
    bool isDown() const { return state == FS_DOWN || state == FS_KO; }
    bool threatens(const Fighter& other) const;              // is attacking and close enough to be blocked
    Matrix worldMatrix() const;
    Vector3 bonePosWorld(int bone) const;
    Vector3 chestWorld() const;

private:
    void startAttack(MoveId m);
    bool tryGroundAttack(const InputState& input);
    MoveId pickGroundMove(Button b, const InputState& input) const;
    int hitSide() const;
    void computeBoxes();
    void beginBlend(int frames);
};
