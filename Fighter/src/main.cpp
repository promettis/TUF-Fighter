// ============================================================================
//  TUF FIGHTER - main.cpp
//  Application shell: window, screens (loading, title, character select,
//  versus/loading, fight, results), menus and player input.
// ============================================================================
#include "engine.h"
#include "model.h"
#include "pose.h"
#include "anims.h"
#include "render.h"
#include "fighter.h"
#include "game.h"
#include "ui.h"
#include "audio.h"

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
enum Screen { SCR_BOOT, SCR_TITLE, SCR_SELECT, SCR_VERSUS, SCR_FIGHT };

struct Mannequin            // a posed character shown in the menus
{
    CharacterDef* ch = nullptr;
    std::vector<BonePose> bones;
    std::vector<Matrix> skin;
    Matrix world = MatrixIdentity();
    Vector4 rim{ 1, 1, 1, 0.5f };
    void set(CharacterDef* c, const Pose& p, float x, float z, float yawDeg)
    {
        ch = c;
        if (!c || !c->rig.valid) return;
        c->rig.solve(p, bones);
        c->rig.skinMatrices(bones, skin);
        world = MatrixMultiply(MatrixRotateY(yawDeg * DEG2RAD), MatrixTranslate(x, 0, z));
    }
};

struct Ember { float x, y, speed, size, phase; };

struct App
{
    std::string assetDir;
    Renderer renderer;
    UI ui;
    Environment envFight, envMenu, envPortrait;

    std::vector<std::unique_ptr<CharacterDef>> roster;
    std::vector<std::unique_ptr<ModelLoader>> loaders;
    std::vector<std::unique_ptr<StageDef>> stages;
    ModelLoader stageLoader;
    int stageIndex = 0;

    Screen screen = SCR_BOOT, nextScreen = SCR_BOOT;
    float screenTime = 0;
    float fadeOut = -1.0f;         // >= 0 while fading to `nextScreen`
    float fadeIn = 1.0f;
    float time = 0;
    bool quit = false;

    // title
    int menuIndex = 0;
    bool showControls = false;
    int difficulty = 1;
    bool twoPlayers = false;

    // select
    int cursor[2] = { 0, 1 };
    bool locked[2] = { false, false };
    float lockFlash[2] = { 0, 0 };
    float selectDone = -1;

    // versus
    float versusReady = -1;

    // fight
    Match match;
    bool paused = false;
    int pauseIndex = 0;
    int resultIndex = 0;
    float resultTime = 0;
    float acc = 0;
    InputState pending[2];
    bool showFps = false;

    std::vector<Ember> embers;

    // automated self test (--selftest <dir>)
    bool selftest = false;
    std::string shotDir;
    int autoStep = 0;
    float autoTimer = 0;
    bool autoAccept = false, autoRight = false;
    bool quickTest = false;
    double perfTime = 0; int perfFrames = 0;
    int optTris = 110000; bool optMsaa = true, optShadows = true;
    int optStress = 1;             // draw the 3D scene several times per frame (performance testing)
    int frameNo = 0;
};
static App app;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string upper(std::string s)
{
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static Color tonemapColor(Vector3 c, float exposure)
{
    auto tm = [&](float x) {
        x *= exposure;
        x = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
        return powf(saturate(x), 1.0f / 2.2f);
    };
    return Color{ (unsigned char)(tm(c.x) * 255), (unsigned char)(tm(c.y) * 255), (unsigned char)(tm(c.z) * 255), 255 };
}

static void goTo(Screen s)
{
    if (app.fadeOut >= 0) return;
    app.nextScreen = s;
    app.fadeOut = 0.0f;
}

// ---- menu input (keyboard + any gamepad) ----
struct Nav { bool up, down, left, right, accept, back; };
static bool padPressed(int button)
{
    for (int g = 0; g < 2; g++) if (IsGamepadAvailable(g) && IsGamepadButtonPressed(g, button)) return true;
    return false;
}
static Nav readNav()
{
    Nav n{};
    n.up = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP);
    n.down = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    n.left = IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT);
    n.right = IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
    n.accept = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_F) || IsKeyPressed(KEY_U) ||
               padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || padPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT);
    n.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    if (app.autoAccept) { n.accept = true; app.autoAccept = false; }
    if (app.autoRight) { n.right = true; app.autoRight = false; }
    return n;
}

// ---- fight input ----
static void readFightInput(int player, InputState& held, InputState& pressed)
{
    bool solo = !app.twoPlayers;
    auto down = [](int k) { return IsKeyDown(k); };
    auto hit = [](int k) { return IsKeyPressed(k); };
    if (player == 0)
    {
        held.left = down(KEY_A); held.right = down(KEY_D); held.up = down(KEY_W); held.down = down(KEY_S);
        pressed.lp = hit(KEY_F); pressed.hp = hit(KEY_G); pressed.sp = hit(KEY_H);
        pressed.lk = hit(KEY_C); pressed.hk = hit(KEY_V); pressed.super = hit(KEY_B);
        if (solo)
        {
            held.left |= down(KEY_LEFT); held.right |= down(KEY_RIGHT); held.up |= down(KEY_UP); held.down |= down(KEY_DOWN);
            pressed.lp |= hit(KEY_U); pressed.hp |= hit(KEY_I); pressed.sp |= hit(KEY_O);
            pressed.lk |= hit(KEY_J); pressed.hk |= hit(KEY_K); pressed.super |= hit(KEY_L);
        }
    }
    else
    {
        held.left = down(KEY_LEFT); held.right = down(KEY_RIGHT); held.up = down(KEY_UP); held.down = down(KEY_DOWN);
        pressed.lp = hit(KEY_I) || hit(KEY_KP_4); pressed.hp = hit(KEY_O) || hit(KEY_KP_5); pressed.sp = hit(KEY_P) || hit(KEY_KP_6);
        pressed.lk = hit(KEY_K) || hit(KEY_KP_1); pressed.hk = hit(KEY_L) || hit(KEY_KP_2); pressed.super = hit(KEY_SEMICOLON) || hit(KEY_KP_3);
    }
    int g = player;
    if (IsGamepadAvailable(g))
    {
        float ax = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_Y);
        held.left |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || ax < -0.5f;
        held.right |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax > 0.5f;
        held.up |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_UP) || ay < -0.6f;
        held.down |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ay > 0.6f;
        pressed.lp |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
        pressed.hp |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        pressed.lk |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        pressed.hk |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        pressed.sp |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
        pressed.super |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
    }
}

// ---------------------------------------------------------------------------
// Content discovery: every .glb in the character / stage folders is used,
// so new fighters and stages can be added by dropping files in.
// ---------------------------------------------------------------------------
static void applyKnownStats(CharacterDef& c)
{
    std::string n = upper(c.name);
    c.title = "CHALLENGER"; c.accent = Color{ 255, 190, 60, 255 };
    c.statPower = 3; c.statSpeed = 3; c.statDefense = 3;
    c.shotName = "ENERGY SHOT"; c.superName = "POWER BEAM";
    c.fxA = Color{ 200, 90, 255, 255 }; c.fxB = Color{ 255, 200, 255, 255 };
    if (n == "TAHA")
    {
        c.shotName = "INFERNO SHOT"; c.superName = "BLAZE CANNON";
        c.fxA = Color{ 255, 110, 30, 255 }; c.fxB = Color{ 255, 225, 120, 255 };
        c.shotSpeed = 7.2f; c.shotDamage = 95;
        c.title = "HEAVY HITTER"; c.power = 1.10f; c.speed = 0.95f; c.health = 1000;
        c.statPower = 5; c.statSpeed = 3; c.statDefense = 4; c.accent = Color{ 255, 96, 60, 255 };
    }
    else if (n == "SAAD")
    {
        c.shotName = "FLASH BOLT"; c.superName = "THUNDER RAY";
        c.fxA = Color{ 255, 215, 50, 255 }; c.fxB = Color{ 255, 255, 215, 255 };
        c.shotSpeed = 10.0f; c.shotDamage = 70;
        c.title = "SWIFT STRIKER"; c.power = 0.95f; c.speed = 1.12f; c.health = 950;
        c.statPower = 3; c.statSpeed = 5; c.statDefense = 3; c.accent = Color{ 255, 200, 80, 255 };
    }
    else if (n == "UMAR")
    {
        c.shotName = "AZURE ORB"; c.superName = "TIDAL WAVE";
        c.fxA = Color{ 50, 140, 255, 255 }; c.fxB = Color{ 175, 230, 255, 255 };
        c.shotSpeed = 8.4f; c.shotDamage = 82;
        c.title = "IRON GUARD"; c.power = 1.00f; c.speed = 0.98f; c.health = 1080;
        c.statPower = 4; c.statSpeed = 3; c.statDefense = 5; c.accent = Color{ 90, 170, 255, 255 };
    }
}

static void scanContent()
{
    std::string charDir = app.assetDir + "/Rigged 3D character models";
    if (DirectoryExists(charDir.c_str()))
    {
        FilePathList list = LoadDirectoryFilesEx(charDir.c_str(), ".glb", false);
        std::vector<std::string> files;
        for (unsigned int i = 0; i < list.count; i++) files.push_back(list.paths[i]);
        UnloadDirectoryFiles(list);
        std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
            // keep the original trio in a fixed order, anything new comes after
            auto rank = [](const std::string& p) {
                std::string n = upper(GetFileNameWithoutExt(p.c_str()));
                return n == "TAHA" ? 0 : n == "SAAD" ? 1 : n == "UMAR" ? 2 : 3;
            };
            int ra = rank(a), rb = rank(b);
            return ra != rb ? ra < rb : a < b;
        });
        for (const std::string& f : files)
        {
            if (app.roster.size() >= 10) break;
            auto c = std::make_unique<CharacterDef>();
            c->file = f;
            c->name = upper(GetFileNameWithoutExt(f.c_str()));
            c->data.path = f;
            c->data.targetTriangles = app.optTris;   // very dense scans are simplified while loading
            applyKnownStats(*c);
            // portrait photo: Assets/Portraits/<Name>.png, or any picture with the same name next to the model
            std::string stem = GetFileNameWithoutExt(f.c_str());
            std::string cand[4] = { app.assetDir + "/Portraits/" + stem + ".png", charDir + "/" + stem + ".png", charDir + "/" + stem + ".jpg", charDir + "/" + stem + ".jpeg" };
            for (const std::string& ph : cand)
            {
                if (!FileExists(ph.c_str())) continue;
                Image img = LoadImage(ph.c_str());
                if (!img.data) continue;
                if (img.height > 1024) ImageResize(&img, img.width * 1024 / img.height, 1024);
                c->photo = LoadTextureFromImage(img);
                UnloadImage(img);
                GenTextureMipmaps(&c->photo);
                SetTextureFilter(c->photo, TEXTURE_FILTER_TRILINEAR);
                break;
            }
            app.roster.push_back(std::move(c));
        }
    }
    std::string stageDir = app.assetDir + "/Stages";
    if (DirectoryExists(stageDir.c_str()))
    {
        FilePathList list = LoadDirectoryFilesEx(stageDir.c_str(), ".glb", false);
        for (unsigned int i = 0; i < list.count; i++)
        {
            auto s = std::make_unique<StageDef>();
            s->file = list.paths[i];
            s->name = upper(GetFileNameWithoutExt(list.paths[i]));
            s->data.path = s->file;
            // the university block is rebuilt in code from photos of the real building (stage_bloc.cpp)
            if (s->name.find("UNIVERSITY") != std::string::npos) s->procedural = true;
            app.stages.push_back(std::move(s));
        }
        UnloadDirectoryFiles(list);
    }
    bool haveBloc = false;
    for (auto& s : app.stages) haveBloc = haveBloc || s->procedural;
    if (!haveBloc)
    {
        auto s = std::make_unique<StageDef>();
        s->name = "THE UNIVERSITY BLOC";
        s->procedural = true;
        app.stages.insert(app.stages.begin(), std::move(s));
    }
    for (size_t i = 0; i < app.stages.size(); i++) if (app.stages[i]->procedural) { app.stageIndex = (int)i; break; }
}

static int usableCount()
{
    int n = 0;
    for (auto& c : app.roster) if (c->usable) n++;
    return n;
}
static CharacterDef* usableAt(int i)
{
    for (auto& c : app.roster) if (c->usable && i-- == 0) return c.get();
    return nullptr;
}
static StageDef* currentStage() { return app.stages.empty() ? nullptr : app.stages[app.stageIndex].get(); }

// Head shot used by the HUD and the character select grid
static void renderPortrait(CharacterDef& c)
{
    c.portrait = LoadRenderTexture(384, 384);
    SetTextureFilter(c.portrait.texture, TEXTURE_FILTER_BILINEAR);
    Pose p;
    p.headRot = v3(0, -14, 0);
    p.chestRot = v3(0, -10, 0);
    std::vector<BonePose> bones; std::vector<Matrix> skin;
    c.rig.solve(p, bones);
    c.rig.skinMatrices(bones, skin);
    Vector3 head = bones[c.rig.head].pos + v3(0, 0.085f, 0.02f);
    Camera3D cam{};
    cam.target = head;
    cam.position = head + v3(-0.14f, 0.02f, 0.80f);
    cam.up = v3(0, 1, 0); cam.fovy = 28; cam.projection = CAMERA_PERSPECTIVE;
    Environment keep = app.renderer.env;
    app.renderer.env = app.envPortrait;
    app.renderer.clearShadows();
    BeginTextureMode(c.portrait);
    ClearBackground(BLANK);
    app.renderer.beginScene(cam, false);
    SurfaceParams sp;
    app.renderer.drawSkinned(c.gpu, skin, MatrixIdentity(), sp);
    app.renderer.endScene();
    EndTextureMode();
    app.renderer.env = keep;
}

// ---------------------------------------------------------------------------
// Shared menu visuals
// ---------------------------------------------------------------------------
static void drawBackdrop(float glowRed = 1.0f, float glowBlue = 1.0f)
{
    UI& ui = app.ui;
    Color fog = tonemapColor(app.envMenu.horizonColor, app.envMenu.exposure);
    Color top = Color{ 10, 12, 26, 255 };
    ui.rectGradV(0, 0, ui.W, ui.H * 0.62f, top, fog);
    ui.rect(0, ui.H * 0.62f, ui.W, ui.H * 0.38f, fog);
    // slow diagonal light bars
    float t = app.time;
    for (int i = 0; i < 9; i++)
    {
        float x = fmodf(i * 330.0f + t * 22.0f, ui.W + 600.0f) - 400.0f;
        float w = 70.0f + 60.0f * ((i * 37) % 5) / 4.0f;
        Color c = (i % 2) ? Color{ 255, 70, 50, 14 } : Color{ 70, 120, 255, 13 };
        ui.para(x, 0, w, ui.H * 0.70f, 260, c);
    }
    ui.glowAt(ui.W * 0.16f, ui.H * 0.55f, 620, Color{ 255, 50, 30, (unsigned char)(46 * glowRed) });
    ui.glowAt(ui.W * 0.84f, ui.H * 0.55f, 620, Color{ 40, 100, 255, (unsigned char)(46 * glowBlue) });
}

static void drawEmbers(float dt)
{
    UI& ui = app.ui;
    if (app.embers.empty())
    {
        Rng r; r.s = 77;
        for (int i = 0; i < 46; i++) app.embers.push_back({ r.range(0, 1), r.range(0, 1), r.range(0.02f, 0.07f), r.range(1.5f, 4.0f), r.range(0, 6.28f) });
    }
    BeginBlendMode(BLEND_ADDITIVE);
    for (Ember& e : app.embers)
    {
        e.y -= e.speed * dt;
        if (e.y < -0.05f) e.y = 1.05f;
        float x = (e.x + 0.012f * sinf(app.time * 0.8f + e.phase)) * ui.W;
        float a = 0.35f + 0.3f * sinf(app.time * 2.0f + e.phase);
        ui.circle(x, e.y * ui.H, e.size, Color{ 255, 150, 60, (unsigned char)(a * 150) });
    }
    EndBlendMode();
}

// Render posed characters on the dark "showroom" floor
static void drawShowroom(std::vector<Mannequin*> list, Camera3D cam)
{
    Renderer& r = app.renderer;
    r.env = app.envMenu;
    r.env.time = app.time;
    r.beginShadows(v3(0, 0.9f, 0), 5.5f);
    for (Mannequin* m : list) if (m->ch && m->ch->usable) r.shadowSkinned(m->ch->gpu, m->skin, m->world);
    r.endShadows();
    r.beginScene(cam, false);
    r.drawGround(60.0f);
    for (Mannequin* m : list)
    {
        if (!m->ch || !m->ch->usable) continue;
        SurfaceParams sp;
        sp.rim = m->rim;
        r.drawSkinned(m->ch->gpu, m->skin, m->world, sp);
    }
    r.endScene();
}

static void drawLogo(float cx, float y, float size, float alpha)
{
    UI& ui = app.ui;
    const char* a = "TUF";
    const char* b = "FIGHTER";
    float wa = ui.measure(ui.title, a, size, 2), wb = ui.measure(ui.title, b, size, 2);
    float gap = size * 0.16f;
    float x = cx - (wa + gap + wb) * 0.5f;
    Color dark = withAlpha(Color{ 14, 6, 8, 255 }, alpha);
    ui.textOutline(ui.title, a, x, y, size, withAlpha(Color{ 255, 62, 44, 255 }, alpha), dark, size * 0.045f, AL_LEFT, 2);
    ui.textOutline(ui.title, b, x + wa + gap, y, size, withAlpha(Color{ 255, 232, 150, 255 }, alpha), dark, size * 0.045f, AL_LEFT, 2);
    // darker lower half gives the letters a two-tone metallic look
    BeginScissorMode(0, (int)((y + size * 0.56f) * ui.s), GetScreenWidth(), (int)(size * 0.5f * ui.s));
    ui.text(ui.title, a, x, y, size, withAlpha(Color{ 196, 26, 30, 255 }, alpha), AL_LEFT, 2);
    ui.text(ui.title, b, x + wa + gap, y, size, withAlpha(Color{ 255, 176, 46, 255 }, alpha), AL_LEFT, 2);
    EndScissorMode();
    float lw = (wa + gap + wb) * 0.5f;
    ui.rectGradH(cx - lw, y + size * 1.02f, lw, 5, withAlpha(Color{ 255, 62, 44, 0 }, 0), withAlpha(Color{ 255, 200, 90, 255 }, alpha));
    ui.rectGradH(cx, y + size * 1.02f, lw, 5, withAlpha(Color{ 255, 200, 90, 255 }, alpha), withAlpha(Color{ 255, 62, 44, 0 }, 0));
}

static void drawMenuList(const std::vector<std::string>& items, int index, float cx, float y, float step, float size)
{
    UI& ui = app.ui;
    for (size_t i = 0; i < items.size(); i++)
    {
        bool sel = ((int)i == index);
        float yy = y + i * step;
        if (sel)
        {
            float w = ui.measure(ui.heavy, items[i].c_str(), size) + 150.0f;
            float pulse = 0.5f + 0.5f * sinf(app.time * 6.0f);
            ui.para(cx - w * 0.5f, yy - 6, w, size + 14, 22, Color{ 235, 50, 40, (unsigned char)(200 + 40 * pulse) });
            ui.para(cx - w * 0.5f, yy - 6, 14, size + 14, 22, Color{ 255, 220, 120, 255 });
            ui.textShadow(ui.heavy, items[i].c_str(), cx + 6, yy, size, WHITE, AL_CENTER, 3);
        }
        else ui.textShadow(ui.heavy, items[i].c_str(), cx, yy, size, Color{ 190, 196, 214, 255 }, AL_CENTER, 3);
    }
}

static void drawControlsPanel(float cx, float cy)
{
    UI& ui = app.ui;
    float w = 1500, h = 880, x = cx - w * 0.5f, y = cy - h * 0.5f;
    ui.rect(x - 6, y - 6, w + 12, h + 12, Color{ 255, 255, 255, 40 });
    ui.rect(x, y, w, h, Color{ 10, 12, 22, 244 });
    ui.rect(x, y, w, 70, Color{ 200, 40, 34, 255 });
    ui.text(ui.heavy, "HOW TO PLAY", x + 30, y + 6, 56, WHITE);
    ui.text(ui.body, "ENTER / ESC  close", x + w - 30, y + 20, 30, Color{ 255, 220, 200, 255 }, AL_RIGHT);
    const char* rows[9][4] = {
        { "", "1 PLAYER", "2 PLAYERS - P1", "2 PLAYERS - P2" },
        { "MOVE / JUMP / CROUCH", "W A S D  or arrows", "W A S D", "ARROW KEYS" },
        { "LIGHT PUNCH", "U", "F", "I   (NUM 4)" },
        { "HEAVY PUNCH", "I", "G", "O   (NUM 5)" },
        { "SPECIAL", "O", "H", "P   (NUM 6)" },
        { "LIGHT KICK", "J", "C", "K   (NUM 1)" },
        { "HEAVY KICK", "K", "V", "L   (NUM 2)" },
        { "SUPER  (needs a full meter)", "L", "B", ";   (NUM 3)" },
        { "GAMEPAD", "X  Y  A  B  + RB special, RT super", "", "" } };
    for (int r = 0; r < 9; r++)
    {
        float yy = y + 84 + r * 40;
        Color c = r == 0 ? Color{ 255, 210, 90, 255 } : WHITE;
        ui.text(ui.body, rows[r][0], x + 40, yy, 32, Color{ 170, 178, 200, 255 });
        ui.text(ui.body, rows[r][1], x + 520, yy, 32, c);
        ui.text(ui.body, rows[r][2], x + 960, yy, 32, c);
        ui.text(ui.body, rows[r][3], x + 1210, yy, 32, c);
    }
    float ty = y + 84 + 9 * 40 + 8;
    ui.rect(x + 30, ty, w - 60, 2, Color{ 255, 255, 255, 50 });
    ui.text(ui.heavy, "MOVE LIST", x + 40, ty + 10, 38, Color{ 255, 210, 90, 255 });
    const char* moves[8][2] = {
        { "SPECIAL", "Energy shot - a projectile that flies across the stage" },
        { "FORWARD + SPECIAL", "Rush punch - dashes in and knocks down" },
        { "DOWN + SPECIAL", "Rising upper - invincible anti-air launcher" },
        { "SPECIAL in the air", "Dive kick" },
        { "SUPER", "Beam super - fills the screen, hits many times (meter fills as you fight)" },
        { "FORWARD + HEAVY PUNCH / KICK", "Overhead smash (hits crouch-blockers)  /  push kick" },
        { "DOWN + PUNCH / KICK", "Low jab, uppercut launcher, low kick, sweep" },
        { "COMBOS", "Cancel into something stronger on hit:  LP > LK > HP > SPECIAL > SUPER" } };
    for (int i = 0; i < 8; i++)
    {
        float yy = ty + 58 + i * 36;
        ui.text(ui.body, moves[i][0], x + 40, yy, 29, WHITE);
        ui.text(ui.body, moves[i][1], x + 520, yy, 29, Color{ 200, 208, 226, 255 });
    }
    float by = ty + 58 + 8 * 36 + 8;
    ui.text(ui.body, "BLOCK: hold BACK.  Low attacks must be blocked crouching, jump attacks and the overhead standing.", x + 40, by, 28, Color{ 140, 220, 255, 255 });
    ui.text(ui.body, "F1 collision boxes     F3 frame rate     F11 full screen     M mute     ESC pause", x + 40, by + 34, 28, Color{ 255, 210, 90, 255 });
}

// ---------------------------------------------------------------------------
// BOOT: loading screen while the fighters are prepared
// ---------------------------------------------------------------------------
static float bootProgress = 0.0f;
static std::string bootLabel;

static void updateBoot(float dt)
{
    // start all loader threads on the first frame
    if (app.loaders.empty())
    {
        for (auto& c : app.roster)
        {
            auto l = std::make_unique<ModelLoader>();
            l->start(&c->data);
            app.loaders.push_back(std::move(l));
        }
    }
    // upload at most one finished model per frame so the screen keeps animating
    float sum = 0; bool all = true; bool uploaded = false;
    bootLabel.clear();
    for (auto& c : app.roster)
    {
        float p = c->data.progress * 0.9f;
        if (c->data.done)
        {
            if (!c->data.error.empty()) p = 1.0f;
            else if (!c->gpu.ready)
            {
                if (!uploaded && app.screenTime > 0.35f)
                {
                    c->gpu.upload(c->data);
                    c->usable = c->data.skinned && c->rig.build(c->data);
                    if (c->usable) renderPortrait(*c);
                    uploaded = true;
                    p = 1.0f;
                }
                else all = false;
            }
            else p = 1.0f;
        }
        else all = false;
        if (p < 1.0f && bootLabel.empty()) bootLabel = c->name;
        sum += p;
    }
    float target = app.roster.empty() ? 1.0f : sum / app.roster.size();
    bootProgress = std::max(bootProgress, approach(bootProgress, target, 6.0f, dt));
    if (all && bootProgress > 0.985f && app.screenTime > 1.8f && app.fadeOut < 0) { musicPlay(MUSIC_MENU); goTo(SCR_TITLE); }
}

static void drawLoadingBar(float cx, float y, float w, float progress, const char* label)
{
    UI& ui = app.ui;
    float x = cx - w * 0.5f;
    ui.para(x - 6, y - 6, w + 12, 30, 12, Color{ 0, 0, 0, 200 });
    ui.para(x, y, w, 18, 8, Color{ 40, 42, 58, 255 });
    ui.paraGradH(x, y, w * saturate(progress), 18, 8, Color{ 235, 50, 40, 255 }, Color{ 255, 205, 80, 255 });
    float gx = x + w * saturate(progress);
    ui.glowAt(gx, y + 9, 46, Color{ 255, 200, 110, 150 });
    ui.text(ui.body, label, x + 4, y - 50, 34, Color{ 214, 220, 235, 255 });
    ui.text(ui.heavy, TextFormat("%d%%", (int)(saturate(progress) * 100.0f + 0.5f)), x + w, y - 58, 44, WHITE, AL_RIGHT);
}

static void drawBoot(float dt)
{
    UI& ui = app.ui;
    drawBackdrop();
    drawEmbers(dt);
    float a = smooth01(app.screenTime / 0.6f);
    drawLogo(ui.W * 0.5f, 330 - 20 * (1 - a), 230, a);
    ui.text(ui.body, "WHO RULES THE BLOC?", ui.W * 0.5f, 590, 44, withAlpha(Color{ 200, 206, 226, 255 }, a), AL_CENTER, 10);
    std::string label = bootLabel.empty() ? "GETTING READY..." : ("LOADING FIGHTER   " + bootLabel);
    drawLoadingBar(ui.W * 0.5f, 900, std::min(1100.0f, ui.W - 300.0f), bootProgress, label.c_str());
    ui.drawVignette(0.55f);
}

// ---------------------------------------------------------------------------
// TITLE
// ---------------------------------------------------------------------------
static Mannequin titleDolls[3];

static void updateTitle(float)
{
    Nav n = readNav();
    if (app.fadeOut >= 0) return;
    if (app.showControls)
    {
        if (n.accept || n.back) { app.showControls = false; sfxPlay(SFX_UI_BACK); }
        return;
    }
    const int count = 5;
    if (n.up) { app.menuIndex = (app.menuIndex + count - 1) % count; sfxPlay(SFX_UI_MOVE); }
    if (n.down) { app.menuIndex = (app.menuIndex + 1) % count; sfxPlay(SFX_UI_MOVE); }
    if (app.menuIndex == 2 && (n.left || n.right))
    {
        app.difficulty = (app.difficulty + (n.right ? 1 : 2)) % 3;
        sfxPlay(SFX_UI_MOVE);
    }
    if (n.accept)
    {
        sfxPlay(SFX_UI_SELECT);
        if (app.menuIndex == 0 || app.menuIndex == 1)
        {
            if (usableCount() == 0) return;
            app.twoPlayers = (app.menuIndex == 1);
            goTo(SCR_SELECT);
        }
        else if (app.menuIndex == 2) app.difficulty = (app.difficulty + 1) % 3;
        else if (app.menuIndex == 3) app.showControls = true;
        else app.quit = true;
    }
}

static void drawTitle(float dt)
{
    UI& ui = app.ui;
    drawBackdrop();
    // the fighters lined up behind the logo
    int n = std::min(usableCount(), 3);
    std::vector<Mannequin*> list;
    const float xs[3] = { 0.10f, -0.95f, 1.05f }, zs[3] = { 0.55f, 0.0f, 0.0f };
    for (int i = 0; i < n; i++)
    {
        Pose p = animEval(ANIM_IDLE, app.time * 60.0f + i * 31.0f);
        float yaw = 30.0f;
        if (i == 0) { p = animEval(ANIM_INTRO, 0); p.hipsPos.y += 0.006f * sinf(app.time * 1.7f); p.chestRot.x += 1.2f * sinf(app.time * 1.7f); yaw = 14.0f; }
        if (i == 2) { p = poseMirror(p); yaw = -30.0f; }
        titleDolls[i].set(usableAt(i), p, xs[i], zs[i], yaw);
        titleDolls[i].rim = i == 1 ? Vector4{ 1.0f, 0.30f, 0.20f, 1.3f } : i == 2 ? Vector4{ 0.25f, 0.5f, 1.0f, 1.3f } : Vector4{ 1.0f, 0.85f, 0.6f, 0.9f };
        list.push_back(&titleDolls[i]);
    }
    Camera3D cam{};
    // the camera looks past the fighters' left so they stand on the right half of the screen
    cam.position = v3(-1.62f + 0.30f * sinf(app.time * 0.25f), 1.0f, 6.6f);
    cam.target = v3(-1.62f, 1.02f, 0);
    cam.up = v3(0, 1, 0); cam.fovy = 30; cam.projection = CAMERA_PERSPECTIVE;
    drawShowroom(list, cam);
    drawEmbers(dt);

    ui.rectGradH(0, 0, ui.W * 0.62f, ui.H, Color{ 0, 0, 0, 215 }, Color{ 0, 0, 0, 0 });
    ui.rectGradV(0, ui.H - 260, ui.W, 260, Color{ 0, 0, 0, 0 }, Color{ 0, 0, 0, 200 });
    float a = smooth01(app.screenTime / 0.5f);
    float mx = std::min(ui.W * 0.27f, 520.0f);
    drawLogo(mx, 110 - 30 * (1 - a), 170, a);
    ui.text(ui.body, "WHO RULES THE BLOC?", mx, 300, 34, withAlpha(Color{ 214, 220, 235, 255 }, a), AL_CENTER, 9);

    static const char* DIFF[3] = { "EASY", "NORMAL", "HARD" };
    std::vector<std::string> items = { "VS CPU", "VS PLAYER", std::string("DIFFICULTY:  < ") + DIFF[app.difficulty] + " >", "HOW TO PLAY", "QUIT" };
    drawMenuList(items, app.menuIndex, mx, 450, 84, 62);
    ui.text(ui.body, "W / S  select      ENTER  confirm      F11  full screen      M  mute", mx, ui.H - 56, 28, Color{ 150, 158, 180, 255 }, AL_CENTER);
    ui.drawVignette(0.5f);
    if (app.showControls) { ui.fade(0.6f); drawControlsPanel(ui.W * 0.5f, ui.H * 0.5f); }
}

// ---------------------------------------------------------------------------
// CHARACTER SELECT
// ---------------------------------------------------------------------------
static Mannequin selectDolls[2];

static void enterSelect()
{
    int n = usableCount();
    app.cursor[0] = std::min(app.cursor[0], n - 1);
    app.cursor[1] = std::min(app.cursor[1], n - 1);
    if (app.cursor[0] == app.cursor[1] && n > 1) app.cursor[1] = (app.cursor[0] + 1) % n;
    app.locked[0] = app.locked[1] = false;
    app.lockFlash[0] = app.lockFlash[1] = 0;
    app.selectDone = -1;
}

static void updateSelect(float dt)
{
    int n = usableCount();
    for (int i = 0; i < 2; i++) app.lockFlash[i] = std::max(0.0f, app.lockFlash[i] - dt * 2.5f);
    if (app.fadeOut >= 0) return;
    if (app.selectDone >= 0)
    {
        app.selectDone += dt;
        if (app.selectDone > 0.9f) goTo(SCR_VERSUS);
        return;
    }
    // who is choosing: in 1 player mode the player picks both fighters, one after the other
    auto move = [&](int who, int dir) { app.cursor[who] = (app.cursor[who] + n + dir) % n; sfxPlay(SFX_UI_MOVE); };
    auto lock = [&](int who) { app.locked[who] = true; app.lockFlash[who] = 1.0f; sfxPlay(SFX_UI_SELECT); };
    bool back = false;
    if (!app.twoPlayers)
    {
        Nav nav = readNav();
        int who = app.locked[0] ? 1 : 0;
        if (nav.left) move(who, -1);
        if (nav.right) move(who, 1);
        if (nav.up || nav.down) { app.cursor[who] = GetRandomValue(0, n - 1); sfxPlay(SFX_UI_MOVE); }
        if (nav.accept) lock(who);
        if (nav.back)
        {
            if (app.locked[0]) { app.locked[0] = false; sfxPlay(SFX_UI_BACK); }
            else back = true;
        }
    }
    else
    {
        if (!app.locked[0])
        {
            if (IsKeyPressed(KEY_A)) move(0, -1);
            if (IsKeyPressed(KEY_D)) move(0, 1);
            if (IsKeyPressed(KEY_F) || IsKeyPressed(KEY_G) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) lock(0);
        }
        if (!app.locked[1])
        {
            if (IsKeyPressed(KEY_LEFT)) move(1, -1);
            if (IsKeyPressed(KEY_RIGHT)) move(1, 1);
            if (IsKeyPressed(KEY_I) || IsKeyPressed(KEY_O) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_4) ||
                (IsGamepadAvailable(1) && IsGamepadButtonPressed(1, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) lock(1);
        }
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE))
        {
            if (app.locked[0] || app.locked[1]) { app.locked[0] = app.locked[1] = false; sfxPlay(SFX_UI_BACK); }
            else back = true;
        }
        if (app.autoAccept) { app.autoAccept = false; lock(app.locked[0] ? 1 : 0); }
    }
    if (back) { sfxPlay(SFX_UI_BACK); goTo(SCR_TITLE); return; }
    if (app.locked[0] && app.locked[1]) app.selectDone = 0.0f;
}

static void drawStatBar(float x, float y, const char* label, int value, Color c, bool rightSide)
{
    UI& ui = app.ui;
    float dir = rightSide ? -1.0f : 1.0f;
    ui.text(ui.body, label, x, y, 30, Color{ 180, 188, 210, 255 }, rightSide ? AL_RIGHT : AL_LEFT);
    for (int i = 0; i < 5; i++)
    {
        float px = x + dir * (150.0f + i * 44.0f) - (rightSide ? 38.0f : 0.0f);
        ui.para(px, y + 6, 38, 20, 8, i < value ? c : Color{ 50, 54, 72, 255 });
    }
}

static const Vector3 TECH_RED = { 1.0f, 0.17f, 0.10f };
static const Vector3 TECH_BLUE = { 0.10f, 0.42f, 1.0f };
static const Color SIDE_RED = { 240, 64, 50, 255 };
static const Color SIDE_BLUE = { 70, 140, 255, 255 };

// Posed characters in front of the animated electronic backdrop
static void drawTechroom(std::vector<Mannequin*> list, Camera3D cam, Vector4 padA, Vector4 padB)
{
    Renderer& r = app.renderer;
    r.env = app.envMenu;
    r.env.time = app.time;
    r.drawTechBackdrop(cam, TECH_RED, TECH_BLUE, padA, padB, app.time);
    r.clearShadows();
    r.beginScene(cam, false);
    for (Mannequin* m : list)
    {
        if (!m->ch || !m->ch->usable) continue;
        SurfaceParams sp;
        sp.rim = m->rim;
        r.drawSkinned(m->ch->gpu, m->skin, m->world, sp);
    }
    r.endScene();
}

// "Scanner" panel showing the fighter's photo like a hologram
static void drawHoloPanel(CharacterDef* c, bool right, float x, float y, float w, float h, float appear)
{
    UI& ui = app.ui;
    Color side = right ? SIDE_BLUE : SIDE_RED;
    float a = saturate(appear);
    ui.rectGradV(x, y, w, h, withAlpha(side, 0.05f * a), withAlpha(side, 0.22f * a));
    if (c && c->photo.id)
    {
        Texture2D t = c->photo;
        float ph = h * 0.98f, pw = ph * t.width / t.height;
        if (pw > w * 1.1f) { pw = w * 1.1f; ph = pw * t.height / t.width; }
        Rectangle src = { 0, 0, (float)t.width * (right ? -1.0f : 1.0f), (float)t.height };
        BeginScissorMode((int)(x * ui.s), (int)(y * ui.s), (int)(w * ui.s), (int)(h * ui.s));
        float px = x + (w - pw) * 0.5f, py = y + h - ph;
        // coloured ghost image behind gives a hologram fringe
        ui.texture(t, src, px + (right ? 7 : -7), py, pw, ph, withAlpha(side, 0.45f * a));
        ui.texture(t, src, px, py, pw, ph, withAlpha(colorLerp(WHITE, side, 0.12f), a));
        // scan lines + moving scan bar
        for (float yy = y; yy < y + h; yy += 6.0f) ui.rect(x, yy, w, 1.5f, Color{ 0, 0, 0, 46 });
        float bar = y + fmodf(app.time * 0.35f, 1.0f) * h;
        BeginBlendMode(BLEND_ADDITIVE);
        ui.rectGradV(x, bar - 46, w, 46, withAlpha(side, 0.0f), withAlpha(side, 0.35f * a));
        ui.rect(x, bar, w, 2.5f, withAlpha(WHITE, 0.7f * a));
        EndBlendMode();
        EndScissorMode();
    }
    else
    {
        ui.text(ui.title, "?", x + w * 0.5f, y + h * 0.5f - 130, 260, withAlpha(side, 0.55f * a), AL_CENTER);
    }
    // corner brackets
    float L = 34, T = 4;
    Color fc = withAlpha(colorLerp(side, WHITE, 0.35f), a);
    ui.rect(x, y, L, T, fc); ui.rect(x, y, T, L, fc);
    ui.rect(x + w - L, y, L, T, fc); ui.rect(x + w - T, y, T, L, fc);
    ui.rect(x, y + h - T, L, T, fc); ui.rect(x, y + h - L, T, L, fc);
    ui.rect(x + w - L, y + h - T, L, T, fc); ui.rect(x + w - T, y + h - L, T, L, fc);
    ui.rect(x, y + h + 8, w, 2, withAlpha(side, 0.6f * a));
    const char* tag = "FIGHTER DATA";
    ui.text(ui.body, tag, right ? x + w : x, y - 34, 24, withAlpha(colorLerp(side, WHITE, 0.4f), a), right ? AL_RIGHT : AL_LEFT, 5);
    // little activity ticks
    for (int i = 0; i < 9; i++)
    {
        float on = 0.3f + 0.7f * (0.5f + 0.5f * sinf(app.time * (3.0f + i) + i * 1.7f));
        float tx = right ? x + i * 14.0f : x + w - 10 - i * 14.0f;
        ui.rect(tx, y - 26, 8, 14, withAlpha(side, on * a));
    }
}

static void drawSelect(float dt)
{
    UI& ui = app.ui;
    int n = usableCount();
    (void)dt;

    // 3D previews of the highlighted fighters
    std::vector<Mannequin*> list;
    bool show[2] = { true, app.twoPlayers || app.locked[0] };
    const float dollX = 1.28f, dollZ = 0.3f;
    for (int i = 0; i < 2; i++)
    {
        if (!show[i]) continue;
        CharacterDef* c = usableAt(app.cursor[i]);
        Pose p = app.locked[i] ? animEval(ANIM_IDLE, app.time * 60.0f + i * 40.0f) : animEval(ANIM_INTRO, 0);
        if (!app.locked[i]) { p.hipsPos.y += 0.006f * sinf(app.time * 1.8f); p.chestRot.x += 1.0f * sinf(app.time * 1.8f); }
        if (i == 1) p = poseMirror(p);
        float yaw = (i == 0 ? 1.0f : -1.0f) * (app.locked[i] ? 52.0f : 24.0f);
        selectDolls[i].set(c, p, i == 0 ? -dollX : dollX, dollZ, yaw);
        selectDolls[i].rim = i == 0 ? Vector4{ 1.0f, 0.28f, 0.2f, 1.3f + app.lockFlash[i] * 3 } : Vector4{ 0.25f, 0.5f, 1.0f, 1.3f + app.lockFlash[i] * 3 };
        list.push_back(&selectDolls[i]);
    }
    Camera3D cam{};
    cam.position = v3(0, 1.20f, 6.3f); cam.target = v3(0, 1.08f, 0);
    cam.up = v3(0, 1, 0); cam.fovy = 30; cam.projection = CAMERA_PERSPECTIVE;
    Vector4 padA = { -dollX, 0, dollZ, 0.78f + 0.25f * app.lockFlash[0] };
    Vector4 padB = { dollX, 0, dollZ, show[1] ? 0.78f + 0.25f * app.lockFlash[1] : 0.5f };
    drawTechroom(list, cam, padA, padB);

    ui.rectGradV(0, 0, ui.W, 170, Color{ 0, 0, 0, 200 }, Color{ 0, 0, 0, 0 });
    ui.rectGradV(0, ui.H - 330, ui.W, 330, Color{ 0, 0, 0, 0 }, Color{ 0, 0, 0, 215 });
    const char* header = "CHOOSE YOUR FIGHTER";
    if (!app.twoPlayers && app.locked[0]) header = "CHOOSE YOUR OPPONENT";
    float hw = ui.measure(ui.title, header, 88, 3);
    ui.rectGradH(ui.W * 0.5f - hw * 0.5f - 260, 126, 260, 3, Color{ 255, 255, 255, 0 }, Color{ 255, 90, 70, 255 });
    ui.rect(ui.W * 0.5f - hw * 0.5f, 126, hw, 3, Color{ 255, 255, 255, 230 });
    ui.rectGradH(ui.W * 0.5f + hw * 0.5f, 126, 260, 3, Color{ 90, 160, 255, 255 }, Color{ 255, 255, 255, 0 });
    ui.textOutline(ui.title, header, ui.W * 0.5f, 30, 88, WHITE, Color{ 10, 14, 30, 255 }, 4, AL_CENTER, 3);

    // photo panels, name plates and stats
    for (int i = 0; i < 2; i++)
    {
        bool right = (i == 1);
        CharacterDef* c = show[i] ? usableAt(app.cursor[i]) : nullptr;
        float pw = 310, ph = 384;
        float pxp = right ? ui.W - 60.0f - pw : 60.0f;
        drawHoloPanel(c, right, pxp, 200, pw, ph, 1.0f);
        if (!c) continue;
        float x = right ? ui.W - 60.0f : 60.0f;
        Align al = right ? AL_RIGHT : AL_LEFT;
        Color side = right ? SIDE_BLUE : SIDE_RED;
        const char* tag = right ? (app.twoPlayers ? "PLAYER 2" : "CPU") : "PLAYER 1";
        float tw = ui.measure(ui.heavy, tag, 34) + 44;
        ui.para(right ? x - tw : x, 622, tw, 44, right ? -12.0f : 12.0f, side);
        ui.text(ui.heavy, tag, right ? x - 22 : x + 22, 625, 34, WHITE, al);
        ui.textOutline(ui.title, c->name.c_str(), x, 662, 140, WHITE, Color{ 8, 10, 22, 255 }, 5, al, 2);
        ui.text(ui.body, c->title.c_str(), x + (right ? -4 : 4), 800, 36, c->accent, al, 4);
        drawStatBar(x, 852, "POWER", c->statPower, Color{ 255, 96, 60, 255 }, right);
        drawStatBar(x, 890, "SPEED", c->statSpeed, Color{ 255, 205, 80, 255 }, right);
        drawStatBar(x, 928, "DEFENSE", c->statDefense, Color{ 90, 170, 255, 255 }, right);
        ui.text(ui.body, TextFormat("SPECIAL  %s      SUPER  %s", c->shotName.c_str(), c->superName.c_str()), x, 972, 26, colorLerp(c->fxA, WHITE, 0.35f), al, 2);
        if (app.locked[i])
        {
            float rx = right ? pxp + pw * 0.5f : pxp + pw * 0.5f;
            float pop = 1.0f + app.lockFlash[i] * 0.5f;
            ui.textOutline(ui.title, "READY!", rx, 520 - 40 * (pop - 1.0f), 80 * pop, Color{ 255, 232, 120, 255 }, Color{ 60, 20, 10, 255 }, 4, AL_CENTER, 2);
        }
    }

    // roster cards with the fighters' photos
    float cw = 176, chh = 232, gap = 26;
    float total = n * cw + (n - 1) * gap;
    float x0 = ui.W * 0.5f - total * 0.5f, y0 = 742;
    for (int k = 0; k < n; k++)
    {
        CharacterDef* c = usableAt(k);
        float x = x0 + k * (cw + gap);
        bool s0 = (app.cursor[0] == k), s1 = (app.cursor[1] == k) && show[1];
        float lift = (s0 || s1) ? -16.0f : 0.0f;
        float y = y0 + lift;
        ui.rect(x - 5, y - 5, cw + 10, chh + 10, Color{ 0, 0, 0, 220 });
        ui.rectGradV(x, y, cw, chh, colorLerp(Color{ 20, 26, 48, 255 }, c->accent, 0.45f), Color{ 12, 14, 26, 255 });
        BeginScissorMode((int)(x * ui.s), (int)(y * ui.s), (int)(cw * ui.s), (int)((chh - 46) * ui.s));
        if (c->photo.id)
        {
            Texture2D t = c->photo;
            float pw = cw * 1.12f, ph = pw * t.height / t.width;
            ui.texture(t, Rectangle{ 0, 0, (float)t.width, (float)t.height }, x + (cw - pw) * 0.5f, y + (chh - 46) - ph + 14, pw, ph, (s0 || s1) ? WHITE : Color{ 170, 175, 190, 255 });
        }
        else if (c->portrait.texture.id)
        {
            Texture2D t = c->portrait.texture;
            ui.texture(t, Rectangle{ 0, 0, (float)t.width, -(float)t.height }, x - 8, y - 6, cw + 16, cw + 16, WHITE);
        }
        EndScissorMode();
        ui.rect(x, y + chh - 46, cw, 46, Color{ 8, 10, 20, 240 });
        ui.rect(x, y + chh - 48, cw, 3, c->accent);
        ui.text(ui.heavy, c->name.c_str(), x + cw * 0.5f, y + chh - 45, 42, WHITE, AL_CENTER);
        float pulse = 0.6f + 0.4f * sinf(app.time * 8.0f);
        if (s0)
        {
            Color col = withAlpha(Color{ 255, 70, 54, 255 }, app.locked[0] ? 1.0f : pulse);
            ui.paraLines(x - 4, y - 4, cw + 8, chh + 8, 0, 6, col);
            ui.glowAt(x + cw * 0.5f, y + chh, 150, Color{ 255, 60, 40, 70 });
            ui.rect(x - 4, y - 36, 66, 32, SIDE_RED);
            ui.text(ui.heavy, "P1", x + 12, y - 37, 32, WHITE);
        }
        if (s1)
        {
            Color col = withAlpha(Color{ 80, 150, 255, 255 }, app.locked[1] ? 1.0f : pulse);
            ui.paraLines(x - 11, y - 11, cw + 22, chh + 22, 0, 6, col);
            ui.glowAt(x + cw * 0.5f, y + chh, 150, Color{ 40, 100, 255, 70 });
            const char* t = app.twoPlayers ? "P2" : "CPU";
            float tw = ui.measure(ui.heavy, t, 32) + 28;
            ui.rect(x + cw + 11 - tw, y - 43, tw, 32, SIDE_BLUE);
            ui.text(ui.heavy, t, x + cw + 11 - tw + 14, y - 44, 32, WHITE);
        }
    }
    const char* hint = app.twoPlayers ? "P1:  A / D  choose,  F  confirm          P2:  LEFT / RIGHT  choose,  I  or  ENTER  confirm          ESC  back"
                                      : "A / D  choose        W  random        ENTER  or  U  confirm        ESC  back";
    ui.text(ui.body, hint, ui.W * 0.5f, ui.H - 44, 28, Color{ 170, 182, 210, 255 }, AL_CENTER);
    if (app.selectDone >= 0) ui.fade(std::max(0.0f, 0.5f - app.selectDone * 2.0f), WHITE);
}

// ---------------------------------------------------------------------------
// VERSUS: the pre-fight loading screen (the stage is loaded here)
// ---------------------------------------------------------------------------
static Mannequin versusDolls[2];
static float stageProgress = 0.0f;
static bool versusSound = false;

static void enterVersus()
{
    app.versusReady = -1;
    stageProgress = 0.0f;
    versusSound = false;
    StageDef* st = currentStage();
    if (st && !st->loaded && !st->loading)
    {
        st->loading = true;
        if (!st->procedural) app.stageLoader.start(&st->data);
    }
}

static void updateVersus(float dt)
{
    StageDef* st = currentStage();
    float target = 1.0f;
    if (st && !st->loaded && st->procedural)
    {
        // built in code: geometry + the lettering texture
        target = 0.6f;
        if (app.screenTime > 0.9f)
        {
            st->buildUniversityBloc(app.assetDir + "/Fonts/UnifrakturMaguntia-Book.ttf");
            st->loading = false;
        }
    }
    else if (st && !st->loaded)
    {
        target = st->data.progress * 0.92f;
        if (st->data.done && app.screenTime > 0.9f)
        {
            if (st->data.error.empty())
            {
                st->gpu.upload(st->data);
                st->place();
            }
            st->loaded = true;         // (with an error the fight simply has no background model)
            st->loading = false;
        }
    }
    // the bar never jumps: it fills smoothly and the screen stays for a moment even when loading is instant
    float minTime = saturate(app.screenTime / 2.6f);
    stageProgress = std::max(stageProgress, std::min(approach(stageProgress, target, 5.0f, dt), minTime));
    if (!versusSound && app.screenTime > 0.25f) { sfxPlay(SFX_VS); versusSound = true; }
    bool ready = (!st || st->loaded) && stageProgress > 0.99f;
    if (ready && app.versusReady < 0) app.versusReady = 0;
    if (app.versusReady >= 0)
    {
        app.versusReady += dt;
        if (app.versusReady > 0.45f) goTo(SCR_FIGHT);
    }
}

static void drawVersus(float dt)
{
    UI& ui = app.ui;
    CharacterDef* c[2] = { usableAt(app.cursor[0]), usableAt(app.cursor[1]) };
    float slide = 1.0f - easeOutCubic(app.screenTime / 0.55f);
    std::vector<Mannequin*> list;
    const float dollX = 1.12f;
    for (int i = 0; i < 2; i++)
    {
        Pose p = animEval(ANIM_IDLE, app.time * 60.0f + i * 40.0f);
        if (i == 1) p = poseMirror(p);
        float side = i == 0 ? -1.0f : 1.0f;
        versusDolls[i].set(c[i], p, side * (dollX + slide * 3.0f), 0.2f, -side * 62.0f);
        versusDolls[i].rim = i == 0 ? Vector4{ 1.0f, 0.28f, 0.2f, 1.5f } : Vector4{ 0.25f, 0.5f, 1.0f, 1.5f };
        list.push_back(&versusDolls[i]);
    }
    Camera3D cam{};
    cam.position = v3(0, 1.18f, 5.6f); cam.target = v3(0, 1.12f, 0);
    cam.up = v3(0, 1, 0); cam.fovy = 30; cam.projection = CAMERA_PERSPECTIVE;
    drawTechroom(list, cam, Vector4{ -dollX - slide * 3.0f, 0, 0.2f, 0.8f }, Vector4{ dollX + slide * 3.0f, 0, 0.2f, 0.8f });

    ui.rectGradV(0, 0, ui.W, 220, Color{ 0, 0, 0, 200 }, Color{ 0, 0, 0, 0 });
    ui.rectGradV(0, ui.H - 400, ui.W, 400, Color{ 0, 0, 0, 0 }, Color{ 0, 0, 0, 225 });

    // photo panels slide in from the sides
    for (int i = 0; i < 2; i++)
    {
        bool right = (i == 1);
        float pw = 340, ph = 420;
        float px = (right ? ui.W - 60.0f - pw : 60.0f) + (right ? 1 : -1) * slide * 600.0f;
        drawHoloPanel(c[i], right, px, 210, pw, ph, 1.0f - slide);
    }

    // VS emblem slams in
    float t = saturate((app.screenTime - 0.25f) / 0.28f);
    if (t > 0)
    {
        float sc = 1.0f + (1.0f - easeOutBack(t)) * 2.2f;
        float cx = ui.W * 0.5f, cy = 330;
        ui.glowAt(cx, cy, 300 * sc, Color{ 255, 140, 60, (unsigned char)(120 * t) });
        ui.textPop(ui.title, "VS", cx, cy, 250 * sc, withAlpha(Color{ 255, 222, 120, 255 }, t), withAlpha(Color{ 120, 16, 12, 255 }, t), 9, 2);
    }
    for (int i = 0; i < 2; i++)
    {
        if (!c[i]) continue;
        bool right = (i == 1);
        float x = (right ? ui.W - 60.0f : 60.0f) + (right ? 1 : -1) * slide * 500.0f;
        Align al = right ? AL_RIGHT : AL_LEFT;
        Color side = right ? SIDE_BLUE : SIDE_RED;
        const char* tag = right ? (app.twoPlayers ? "PLAYER 2" : "CPU") : "PLAYER 1";
        ui.text(ui.heavy, tag, x, 668, 40, side, al, 2);
        ui.textOutline(ui.title, c[i]->name.c_str(), x, 700, 170, WHITE, Color{ 8, 10, 22, 255 }, 5, al, 2);
        ui.text(ui.body, c[i]->title.c_str(), x + (right ? -4 : 4), 864, 38, c[i]->accent, al, 4);
    }
    StageDef* st = currentStage();
    std::string stageName = st ? st->name : "TRAINING GROUND";
    ui.text(ui.body, "STAGE", ui.W * 0.5f, 52, 30, Color{ 170, 178, 200, 255 }, AL_CENTER, 8);
    ui.textShadow(ui.heavy, stageName.c_str(), ui.W * 0.5f, 84, 62, WHITE, AL_CENTER, 3, Color{ 0, 0, 0, 200 }, 2);

    static const char* TIPS[6] = {
        "TIP:  Hold BACK to block.  Low attacks must be blocked crouching.",
        "TIP:  Cancel light attacks into heavy ones for combos:  LP > LK > HP > HK",
        "TIP:  DOWN + HEAVY PUNCH is an uppercut - perfect against jump-ins.",
        "TIP:  DOWN + HEAVY KICK sweeps the opponent off their feet.",
        "TIP:  Press SPECIAL to throw an energy shot.  Hold FORWARD or DOWN for other specials.",
        "TIP:  When the SUPER meter is full, press SUPER to unleash a screen-filling beam." };
    static int tip = 0;
    if (app.screenTime < dt * 1.5f) tip = GetRandomValue(0, 5);
    drawLoadingBar(ui.W * 0.5f, 990, std::min(900.0f, ui.W - 400.0f), stageProgress, stageProgress < 0.995f ? "LOADING STAGE..." : "READY");
    ui.text(ui.body, TIPS[tip], ui.W * 0.5f, 1022, 28, Color{ 190, 196, 214, 255 }, AL_CENTER);
}

// ---------------------------------------------------------------------------
// FIGHT
// ---------------------------------------------------------------------------
static void enterFight()
{
    app.match.start(usableAt(app.cursor[0]), usableAt(app.cursor[1]), app.selftest, !app.twoPlayers, app.difficulty, currentStage());
    app.paused = false; app.pauseIndex = 0; app.resultIndex = 0; app.resultTime = 0;
    app.acc = 0;
    app.pending[0] = app.pending[1] = InputState{};
    app.showControls = false;
    musicPlay(MUSIC_FIGHT);
}

static void updateFight(float dt)
{
    if (IsKeyPressed(KEY_F1)) app.match.debugBoxes = !app.match.debugBoxes;
    if (app.fadeOut >= 0) return;

    // ---- match finished: result menu ----
    if (app.match.over())
    {
        app.resultTime += dt;
        app.match.frame(dt);
        // keep the winner animating
        static float racc = 0; racc += dt;
        InputState none[2];
        while (racc >= TICK_DT) { racc -= TICK_DT; for (Fighter& p : app.match.f) { p.update(none[0], app.match.f[1 - p.index]); p.updatePose(); } }
        if (app.resultTime < 0.6f) return;
        Nav n = readNav();
        if (n.up) { app.resultIndex = (app.resultIndex + 2) % 3; sfxPlay(SFX_UI_MOVE); }
        if (n.down) { app.resultIndex = (app.resultIndex + 1) % 3; sfxPlay(SFX_UI_MOVE); }
        if (n.accept)
        {
            sfxPlay(SFX_UI_SELECT);
            if (app.resultIndex == 0) goTo(SCR_FIGHT);
            else if (app.resultIndex == 1) { musicPlay(MUSIC_MENU); goTo(SCR_SELECT); }
            else { musicPlay(MUSIC_MENU); goTo(SCR_TITLE); }
        }
        return;
    }

    // ---- pause menu ----
    bool pauseKey = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || padPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT);
    if (app.paused)
    {
        if (app.showControls)
        {
            Nav n = readNav();
            if (n.accept || n.back) { app.showControls = false; sfxPlay(SFX_UI_BACK); }
            return;
        }
        Nav n = readNav();
        if (n.up) { app.pauseIndex = (app.pauseIndex + 3) % 4; sfxPlay(SFX_UI_MOVE); }
        if (n.down) { app.pauseIndex = (app.pauseIndex + 1) % 4; sfxPlay(SFX_UI_MOVE); }
        if (pauseKey) { app.paused = false; sfxPlay(SFX_UI_BACK); return; }
        if (n.accept)
        {
            sfxPlay(SFX_UI_SELECT);
            if (app.pauseIndex == 0) app.paused = false;
            else if (app.pauseIndex == 1) app.showControls = true;
            else if (app.pauseIndex == 2) { musicPlay(MUSIC_MENU); goTo(SCR_SELECT); }
            else { musicPlay(MUSIC_MENU); goTo(SCR_TITLE); }
        }
        return;
    }
    if (pauseKey) { app.paused = true; app.pauseIndex = 0; sfxPlay(SFX_UI_SELECT); return; }

    // ---- gather input: held state is sampled every tick, presses are latched until a tick consumes them ----
    InputState held[2];
    for (int i = 0; i < 2; i++)
    {
        InputState pr;
        readFightInput(i, held[i], pr);
        app.pending[i].lp |= pr.lp; app.pending[i].hp |= pr.hp; app.pending[i].lk |= pr.lk; app.pending[i].hk |= pr.hk;
        app.pending[i].sp |= pr.sp; app.pending[i].super |= pr.super;
    }
    // ---- fixed time step simulation ----
    if (fabsf(dt - TICK_DT) < 0.0025f) dt = TICK_DT;      // hide timer jitter on 60 Hz displays
    app.acc += std::min(dt, 0.1f);
    while (app.acc >= TICK_DT - 1e-6f)
    {
        app.acc -= TICK_DT;
        InputState in[2];
        for (int i = 0; i < 2; i++)
        {
            in[i] = held[i];
            in[i].lp = app.pending[i].lp; in[i].hp = app.pending[i].hp; in[i].lk = app.pending[i].lk; in[i].hk = app.pending[i].hk;
            in[i].sp = app.pending[i].sp; in[i].super = app.pending[i].super;
            app.pending[i] = InputState{};
        }
        app.match.tick(in);
    }
    app.match.frame(dt);
}

static void drawFight(float)
{
    UI& ui = app.ui;
    Renderer& r = app.renderer;
    StageDef* st = currentStage();
    r.env = (st && st->hasEnv) ? st->env : app.envFight;
    r.env.time = app.time;
    for (int i = 0; i < app.optStress; i++) app.match.render(r);
    app.match.drawOverlay(ui);

    if (app.match.over())
    {
        float a = smooth01(app.resultTime / 0.5f);
        ui.fade(0.45f * a);
        float cx = ui.W * 0.5f;
        ui.rectGradH(0, 250, cx, 190, Color{ 0, 0, 0, 0 }, Color{ 0, 0, 0, (unsigned char)(210 * a) });
        ui.rectGradH(cx, 250, ui.W - cx, 190, Color{ 0, 0, 0, (unsigned char)(210 * a) }, Color{ 0, 0, 0, 0 });
        const Fighter& w = app.match.f[app.match.matchWinner];
        bool playerWon = !app.match.cpu[app.match.matchWinner];
        const char* head = app.twoPlayers ? (app.match.matchWinner == 0 ? "PLAYER 1 WINS" : "PLAYER 2 WINS") : (playerWon ? "YOU WIN!" : "YOU LOSE");
        if (app.selftest) head = "MATCH OVER";
        Color hc = playerWon || app.twoPlayers ? Color{ 255, 222, 110, 255 } : Color{ 255, 90, 70, 255 };
        ui.textOutline(ui.title, head, cx, 250, 150, withAlpha(hc, a), withAlpha(Color{ 20, 8, 8, 255 }, a), 6, AL_CENTER, 3);
        ui.textShadow(ui.heavy, TextFormat("%s TAKES THE MATCH  %d - %d", w.ch->name.c_str(), app.match.wins[app.match.matchWinner], app.match.wins[1 - app.match.matchWinner]),
                      cx, 396, 44, withAlpha(WHITE, a), AL_CENTER, 3);
        if (app.resultTime > 0.5f) drawMenuList({ "REMATCH", "CHARACTER SELECT", "MAIN MENU" }, app.resultIndex, cx, 600, 76, 58);
    }
    if (app.paused)
    {
        ui.fade(0.62f);
        if (app.showControls) drawControlsPanel(ui.W * 0.5f, ui.H * 0.5f);
        else
        {
            ui.textOutline(ui.title, "PAUSED", ui.W * 0.5f, 220, 160, WHITE, Color{ 150, 24, 20, 255 }, 6, AL_CENTER, 4);
            drawMenuList({ "RESUME", "HOW TO PLAY", "CHARACTER SELECT", "MAIN MENU" }, app.pauseIndex, ui.W * 0.5f, 470, 80, 60);
        }
    }
}

// ---------------------------------------------------------------------------
// Screen management
// ---------------------------------------------------------------------------
static void enterScreen(Screen s)
{
    app.screen = s;
    app.screenTime = 0;
    app.fadeIn = 1.0f;
    switch (s)
    {
    case SCR_TITLE: app.showControls = false; break;
    case SCR_SELECT: enterSelect(); break;
    case SCR_VERSUS: enterVersus(); break;
    case SCR_FIGHT: enterFight(); break;
    default: break;
    }
}

// scripted run used for automated testing: walks through every screen and saves screenshots
static std::string pendingShot;
static void shot(const char* name) { pendingShot = app.shotDir + "/" + name + ".png"; }
static void runSelfTest(float dt)
{
    app.autoTimer += dt;
    auto next = [&]() { app.autoStep++; app.autoTimer = 0; };
    switch (app.autoStep)
    {
    case 0: if (app.screen == SCR_BOOT && app.screenTime > 0.8f) { shot("01_loading"); next(); } break;
    case 1: if (app.screen == SCR_TITLE && app.screenTime > 1.5f) { shot("02_title"); app.autoAccept = true; next(); } break;
    case 2: if (app.screen == SCR_SELECT && app.screenTime > 1.2f) { shot("03_select"); app.autoAccept = true; next(); } break;
    case 3: if (app.autoTimer > 0.4f) { app.autoRight = true; next(); } break;
    case 4: if (app.autoTimer > 0.5f) { shot("04_select_opponent"); app.autoAccept = true; next(); } break;
    case 5: if (app.autoTimer > 0.4f) { shot("05_select_ready"); next(); } break;
    case 6: if (app.screen == SCR_VERSUS && app.screenTime > 1.4f) { shot("06_versus"); next(); } break;
    case 7: if (app.screen == SCR_FIGHT && app.screenTime > 0.9f) { shot("07_round_intro"); next(); } break;
    case 8: if (app.autoTimer > 1.3f) { shot("08_fight_call"); next(); } break;
    default:
    {
        int k = app.autoStep - 9;
        if (app.match.over() && app.resultTime > 1.2f) { shot("99_result"); app.quit = true; break; }
        if (app.screenTime > 150.0f || (app.quickTest && k >= 9)) { shot("98_end"); app.quit = true; break; }
        if (app.quickTest && k == 1) app.match.f[0].meter = app.match.f[1].meter = 100.0f;
        if (k < 60 && app.autoTimer > 1.1f)
        {
            if (k == 3) app.match.debugBoxes = true;
            if (k == 5) app.match.debugBoxes = false;
            if (k == 5 || k == 14) app.match.f[0].meter = 100.0f;
            if (k == 9 || k == 20) app.match.f[1].meter = 100.0f;
            shot(TextFormat("%02d_fight", 10 + k));
            next();
        }
        break;
    }
    }
}

int main(int argc, char** argv)
{
    bool windowed = false;
    bool quick = false;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--assets" && i + 1 < argc) app.assetDir = argv[++i];
        else if (a == "--selftest" && i + 1 < argc) { app.selftest = true; app.shotDir = argv[++i]; }
        else if (a == "--windowed") windowed = true;
        else if (a == "--quick") quick = true;
        else if (a == "--tris" && i + 1 < argc) app.optTris = atoi(argv[++i]);
        else if (a == "--nomsaa") app.optMsaa = false;
        else if (a == "--stress" && i + 1 < argc) app.optStress = atoi(argv[++i]);
        else if (a == "--noshadows") app.optShadows = false;
    }

    SetTraceLogLevel(LOG_WARNING);
    // (the self test runs without v-sync so it can measure the real rendering speed)
    SetConfigFlags((app.optMsaa ? FLAG_MSAA_4X_HINT : 0) | FLAG_WINDOW_RESIZABLE | (app.selftest ? 0 : FLAG_VSYNC_HINT));
    InitWindow(1280, 720, "TUF Fighter");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(960, 540);
    {
        int mon = GetCurrentMonitor();
        int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
        if (mw > 0 && mh > 0)
        {
            int h = std::min((int)(mh * 0.84f), (int)(mw * 0.84f * 9.0f / 16.0f));
            if (app.selftest) h = std::min(h, 900);
            int w = h * 16 / 9;
            SetWindowSize(w, h);
            SetWindowPosition((mw - w) / 2, (mh - h) / 2);
        }
        int hz = GetMonitorRefreshRate(mon);
        SetTargetFPS(app.selftest ? 0 : (hz >= 50 ? std::min(hz, 240) : 60));
    }
    (void)windowed;
    app.quickTest = quick;

    if (app.assetDir.empty())
    {
        std::string base = GetApplicationDirectory();
        const char* tries[4] = { "Assets", "../Assets", "../../Assets", "Fighter/Assets" };
        for (const char* t : tries)
            if (DirectoryExists((base + t).c_str())) { app.assetDir = base + t; break; }
        if (app.assetDir.empty()) app.assetDir = DirectoryExists("Assets") ? "Assets" : base + "Assets";
    }

    // ---- lighting presets ----
    app.envFight = Environment{};
    app.envFight.sunDir = Vector3Normalize(v3(-0.60f, 0.40f, 0.56f));
    app.envFight.sunColor = v3(1.85f, 1.40f, 0.98f);
    app.envFight.skyColor = v3(0.34f, 0.40f, 0.58f);
    app.envFight.groundColor = v3(0.30f, 0.23f, 0.19f);
    app.envFight.horizonColor = v3(0.98f, 0.66f, 0.44f);
    app.envFight.zenithColor = v3(0.17f, 0.30f, 0.62f);
    app.envFight.fogDensity = 0.0075f;
    app.envMenu = Environment{};
    app.envMenu.sunDir = Vector3Normalize(v3(-0.55f, 0.55f, -0.62f));
    app.envMenu.sunColor = v3(1.9f, 1.35f, 1.0f);
    app.envMenu.skyColor = v3(0.50f, 0.56f, 0.78f);
    app.envMenu.groundColor = v3(0.30f, 0.20f, 0.20f);
    app.envMenu.horizonColor = v3(0.012f, 0.014f, 0.030f);
    app.envMenu.zenithColor = v3(0.01f, 0.012f, 0.03f);
    app.envMenu.fogDensity = 0.085f;
    app.envPortrait = Environment{};
    app.envPortrait.sunDir = Vector3Normalize(v3(-0.4f, 0.5f, 0.8f));
    app.envPortrait.sunColor = v3(1.25f, 1.15f, 1.05f);
    app.envPortrait.skyColor = v3(0.62f, 0.66f, 0.78f);
    app.envPortrait.groundColor = v3(0.40f, 0.34f, 0.32f);
    app.envPortrait.fogDensity = 0.0f;

    bool ok = app.renderer.init();
    app.renderer.shadowsEnabled = app.optShadows;
    app.ui.load(app.assetDir + "/Fonts");
    animInit();
    audioInit(app.assetDir + "/Sounds");
    scanContent();
    enterScreen(SCR_BOOT);

    while (!WindowShouldClose() && !app.quit)
    {
        float dt = app.selftest ? TICK_DT : std::min(GetFrameTime(), 0.1f);
        app.time += dt;
        app.screenTime += dt;
        app.frameNo++;
        app.ui.beginFrame();

        if (IsKeyPressed(KEY_F11) || (IsKeyPressed(KEY_ENTER) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)))) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_M)) audioToggleMute();
        audioUpdate(dt);
        if (IsKeyPressed(KEY_F3)) app.showFps = !app.showFps;
        if (app.selftest) runSelfTest(dt);

        // ---- update ----
        switch (app.screen)
        {
        case SCR_BOOT: updateBoot(dt); break;
        case SCR_TITLE: updateTitle(dt); break;
        case SCR_SELECT: updateSelect(dt); break;
        case SCR_VERSUS: updateVersus(dt); break;
        case SCR_FIGHT: updateFight(dt); break;
        }
        if (app.fadeOut >= 0)
        {
            app.fadeOut += dt / 0.28f;
            if (app.fadeOut >= 1.0f) { app.fadeOut = -1.0f; enterScreen(app.nextScreen); }
        }
        app.fadeIn = std::max(0.0f, app.fadeIn - dt / 0.35f);

        // ---- draw ----
        // (the self test renders only some frames to save time on slow software renderers)
        if (app.selftest && pendingShot.empty() && (app.frameNo % 6) != 0) continue;
        bool timed = app.selftest && pendingShot.empty() && app.screen == SCR_FIGHT;
        double drawStart = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        if (!ok)
        {
            DrawText("Your graphics driver does not support OpenGL 3.3 shaders.", 40, 40, 24, RED);
        }
        else if (app.screen != SCR_BOOT && usableCount() == 0)
        {
            app.ui.text(app.ui.body, "No character models found.", 60, 60, 48, WHITE);
            app.ui.text(app.ui.body, ("Expected rigged .glb files in:  " + app.assetDir + "/Rigged 3D character models").c_str(), 60, 130, 32, Color{ 255, 200, 90, 255 });
        }
        else
        {
            switch (app.screen)
            {
            case SCR_BOOT: drawBoot(dt); break;
            case SCR_TITLE: drawTitle(dt); break;
            case SCR_SELECT: drawSelect(dt); break;
            case SCR_VERSUS: drawVersus(dt); break;
            case SCR_FIGHT: drawFight(dt); break;
            }
        }
        float f = std::max(app.fadeIn, app.fadeOut >= 0 ? app.fadeOut : 0.0f);
        app.ui.fade(f);
        if (app.showFps) DrawFPS(12, GetScreenHeight() - 28);
        if (!pendingShot.empty())
        {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, pendingShot.c_str());
            UnloadImage(img);
            pendingShot.clear();
        }
        EndDrawing();
        if (timed) { app.perfTime += GetTime() - drawStart; app.perfFrames++; }
    }

    if (app.selftest && app.perfFrames > 0)
    {
        std::string txt = TextFormat("fight frames: %d\naverage frame time: %.2f ms\nframes per second (uncapped): %.1f\nwindow: %d x %d\n",
                                     app.perfFrames, app.perfTime / app.perfFrames * 1000.0, app.perfFrames / app.perfTime, GetScreenWidth(), GetScreenHeight());
        SaveFileText((app.shotDir + "/perf.txt").c_str(), txt.c_str());
    }
    // ---- shutdown ----
    for (auto& l : app.loaders) l->join();
    app.stageLoader.join();
    for (auto& c : app.roster) { c->gpu.unload(); if (c->portrait.id) UnloadRenderTexture(c->portrait); if (c->photo.id) UnloadTexture(c->photo); }
    for (auto& s : app.stages) s->unload();
    audioShutdown();
    app.ui.unload();
    app.renderer.shutdown();
    CloseWindow();
    return 0;
}
