// ============================================================================
//  TUF FIGHTER - audio.cpp
//  Tiny software synthesiser: sound effects + two looping music tracks.
// ============================================================================
#include "audio.h"

namespace
{
    constexpr int SR = 44100;
    constexpr int VOICES = 3;                 // overlapping copies of every effect

    struct Effect { Sound s[VOICES]{}; int next = 0; bool ok = false; };
    // file names (without extension) that override the built-in sounds
    const char* SFX_NAMES[SFX_COUNT] = {
        "ui_move", "ui_select", "ui_back", "whoosh_light", "whoosh_heavy", "hit_light", "hit_heavy", "block",
        "jump", "land", "body_fall", "ko", "round", "fight", "vs", "win",
        "special", "charge", "beam", "shot", "explode" };
    std::string soundFolder;
    Music fileMusic[3]{};
    bool  hasFileMusic[3] = { false, false, false };
    int   fileTrack = MUSIC_NONE;
    float fileGain = 0.0f;

    std::string findSoundFile(const std::string& base)
    {
        static const char* EXT[4] = { ".wav", ".ogg", ".mp3", ".flac" };
        for (const char* e : EXT)
        {
            std::string p = soundFolder + "/" + base + e;
            if (FileExists(p.c_str())) return p;
        }
        return "";
    }
    Effect effects[SFX_COUNT];
    bool ready = false, muted = false;

    // music: raw loops played through an audio stream callback (sample accurate looping)
    std::vector<short> loops[3];
    std::atomic<bool> musicReady{ false };
    std::thread musicThread;
    AudioStream stream{};
    std::atomic<int> curTrack{ MUSIC_NONE }, wantTrack{ MUSIC_NONE };
    std::atomic<float> musicGain{ 0.0f }, duck{ 0.0f };
    size_t playPos = 0;
    float cbGain = 0.0f;

    uint32_t rngState = 0x1234567u;
    float frand() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (rngState & 0xFFFFFF) / 8388608.0f - 1.0f; }

    Sound toSound(std::vector<float>& b, float gain)
    {
        float peak = 0.0001f;
        for (float v : b) peak = std::max(peak, fabsf(v));
        std::vector<short> pcm(b.size());
        for (size_t i = 0; i < b.size(); i++) pcm[i] = (short)(clampf(b[i] / peak * gain, -1.0f, 1.0f) * 32000.0f);
        Wave w{};
        w.frameCount = (unsigned int)pcm.size(); w.sampleRate = SR; w.sampleSize = 16; w.channels = 1; w.data = pcm.data();
        return LoadSoundFromWave(w);
    }

    void makeEffect(Sfx id, float seconds, float gain, const std::function<float(float t, float u)>& fn)
    {
        // a file in Assets/Sounds wins over the synthesised version
        std::string file = findSoundFile(SFX_NAMES[id]);
        if (!file.empty())
        {
            Sound snd = LoadSound(file.c_str());
            if (snd.frameCount > 0)
            {
                effects[id].s[0] = snd;
                for (int k = 1; k < VOICES; k++) effects[id].s[k] = LoadSoundAlias(snd);
                effects[id].ok = true;
                return;
            }
        }
        int n = (int)(seconds * SR);
        std::vector<float> b(n);
        for (int i = 0; i < n; i++)
        {
            float t = (float)i / SR;
            float v = fn(t, (float)i / n);
            float fadeOut = std::min(1.0f, (n - i) / (0.008f * SR));   // avoid clicks
            b[i] = v * fadeOut;
        }
        effects[id].s[0] = toSound(b, gain);
        for (int k = 1; k < VOICES; k++) effects[id].s[k] = LoadSoundAlias(effects[id].s[0]);
        effects[id].ok = true;
    }

    // one-pole low pass used inside the effect lambdas
    struct LP { float y = 0; float run(float x, float a) { y += a * (x - y); return y; } };

    // ---------------------------------------------------------------------
    // Music sequencer
    // ---------------------------------------------------------------------
    struct Track
    {
        std::vector<float> buf;
        void add(int start, int len, const std::function<float(int i, float t)>& fn)
        {
            int n = (int)buf.size();
            for (int i = 0; i < len; i++) buf[(start + i) % n] += fn(i, (float)i / SR);
        }
    };
    float noteHz(int semitoneFromA2) { return 110.0f * powf(2.0f, semitoneFromA2 / 12.0f); }
    float saw(float ph) { return 2.0f * (ph - floorf(ph + 0.5f)); }
    float sqr(float ph, float w = 0.5f) { return (ph - floorf(ph)) < w ? 1.0f : -1.0f; }

    void kick(Track& tr, int at, float g)
    {
        tr.add(at, (int)(0.30f * SR), [g](int, float t) {
            float f = 46.0f + 120.0f * expf(-t * 32.0f);
            return sinf(2 * PI * (46.0f * t + 120.0f / 32.0f * (1.0f - expf(-t * 32.0f)))) * expf(-t * 9.0f) * g + (f > 150 ? 0.15f * g : 0.0f);
        });
    }
    void snare(Track& tr, int at, float g)
    {
        auto lp = std::make_shared<LP>();
        tr.add(at, (int)(0.20f * SR), [g, lp](int, float t) {
            float n = frand();
            float hp = n - lp->run(n, 0.18f);
            return (hp * expf(-t * 22.0f) * 0.8f + sinf(2 * PI * 190.0f * t) * expf(-t * 30.0f) * 0.5f) * g;
        });
    }
    void hat(Track& tr, int at, float g, float decay)
    {
        auto lp = std::make_shared<LP>();
        tr.add(at, (int)(0.09f * SR), [g, lp, decay](int, float t) {
            float n = frand();
            return (n - lp->run(n, 0.55f)) * expf(-t * decay) * g;
        });
    }
    void bass(Track& tr, int at, int len, float hz, float g)
    {
        auto lp = std::make_shared<LP>();
        tr.add(at, len, [=](int i, float t) {
            float env = std::min(1.0f, t * 200.0f) * std::min(1.0f, (len - i) / (0.01f * SR));
            float v = saw(hz * t) * 0.6f + sqr(hz * 0.5f * t) * 0.4f;
            return lp->run(v, 0.03f + 0.20f * expf(-t * 9.0f)) * env * g;
        });
    }
    void stab(Track& tr, int at, int len, float hz, float g, float bright)
    {
        auto lp = std::make_shared<LP>();
        tr.add(at, len, [=](int i, float t) {
            float env = std::min(1.0f, t * 300.0f) * expf(-t * 5.0f) * std::min(1.0f, (len - i) / (0.02f * SR));
            float v = saw(hz * t) + saw(hz * 1.006f * t) + 0.5f * sqr(hz * 2.0f * t, 0.3f);
            return lp->run(v, bright) * env * g * 0.4f;
        });
    }
    void pad(Track& tr, int at, int len, float hz, float g)
    {
        auto lp = std::make_shared<LP>();
        tr.add(at, len, [=](int i, float t) {
            float a = std::min(1.0f, t / 0.6f) * std::min(1.0f, (len - i) / (0.7f * SR));
            float v = saw(hz * t) + saw(hz * 1.004f * t) + saw(hz * 0.996f * t);
            return lp->run(v, 0.035f + 0.015f * sinf(t * 0.9f)) * a * g * 0.3f;
        });
    }
    void pluck(Track& tr, int at, float hz, float g)
    {
        tr.add(at, (int)(0.5f * SR), [=](int, float t) {
            return (sinf(2 * PI * hz * t) + 0.4f * sinf(2 * PI * hz * 2.0f * t) * expf(-t * 12.0f)) * expf(-t * 7.0f) * std::min(1.0f, t * 400.0f) * g;
        });
    }

    std::vector<short> finish(Track& tr, float gain)
    {
        float peak = 0.0001f;
        for (float v : tr.buf) peak = std::max(peak, fabsf(v));
        std::vector<short> out(tr.buf.size());
        for (size_t i = 0; i < out.size(); i++) out[i] = (short)(tanhf(tr.buf[i] / peak * 1.6f) * gain * 32000.0f);
        return out;
    }

    // Driving fight theme, E minor, 132 bpm, 8 bars
    std::vector<short> makeFightMusic()
    {
        const float bpm = 132.0f;
        const int step = (int)(SR * 60.0f / bpm / 4.0f);          // one 16th note
        const int bars = 8, steps = bars * 16;
        Track tr; tr.buf.assign((size_t)step * steps, 0.0f);
        // roots per bar (semitones from A2): E, E, C, D, E, E, G, B
        const int roots[8] = { -5, -5, -9, -7, -5, -5, -2, 2 };
        const char* bassPat = "x.xx..x.x.xx.x.x";
        const char* kickPat = "x...x..xx...x.x.";
        for (int b = 0; b < bars; b++)
        {
            float root = noteHz(roots[b] - 12);
            for (int st = 0; st < 16; st++)
            {
                int at = (b * 16 + st) * step;
                if (kickPat[st] == 'x') kick(tr, at, 1.0f);
                if (st == 4 || st == 12) snare(tr, at, 0.75f);
                if (b % 4 == 3 && st >= 13) snare(tr, at, 0.45f + 0.1f * (st - 13));
                hat(tr, at, (st % 4 == 2) ? 0.30f : 0.16f, (st % 4 == 2) ? 30.0f : 70.0f);
                if (bassPat[st] == 'x')
                {
                    float hz = root * ((st == 7 || st == 13) ? 2.0f : 1.0f);
                    bass(tr, at, (int)(step * 0.9f), hz, 0.70f);
                }
            }
            // power chord stabs on the off beats
            const int hits[3] = { 0, 6, 10 };
            for (int h : hits)
            {
                int at = (b * 16 + h) * step;
                float r = noteHz(roots[b]);
                stab(tr, at, step * 3, r, 0.50f, 0.22f);
                stab(tr, at, step * 3, r * 1.4983f, 0.40f, 0.22f);
                stab(tr, at, step * 3, r * 2.0f, 0.30f, 0.22f);
            }
        }
        // lead riff (E minor pentatonic), second half adds a higher answer phrase
        const int riff[16] = { 7, -99, 10, 7, -99, 12, 10, -99, 7, -99, 5, 7, -99, 3, 5, -99 };
        const int riff2[16] = { 14, -99, 12, 14, -99, 17, 15, -99, 14, -99, 12, 10, -99, 12, 14, 19 };
        for (int b = 0; b < bars; b++)
        {
            const int* r = (b >= 4 && (b % 2 == 1)) ? riff2 : riff;
            if (b % 2 == 0 && b < 4) continue;                       // leave some space
            for (int st = 0; st < 16; st++)
            {
                if (r[st] == -99) continue;
                int at = (b * 16 + st) * step;
                float hz = noteHz(r[st] + 12);
                auto lp = std::make_shared<LP>();
                int len = step * 2;
                tr.add(at, len, [=](int i, float t) {
                    float env = std::min(1.0f, t * 250.0f) * expf(-t * 4.0f) * std::min(1.0f, (len - i) / (0.01f * SR));
                    float vib = 1.0f + 0.004f * sinf(t * 34.0f);
                    return lp->run(sqr(hz * vib * t, 0.35f) + 0.5f * saw(hz * 0.5f * t), 0.25f) * env * 0.26f;
                });
                // echo
                tr.add(at + step * 3, len, [=](int i, float t) {
                    float env = std::min(1.0f, t * 250.0f) * expf(-t * 5.0f) * std::min(1.0f, (len - i) / (0.01f * SR));
                    return sqr(hz * t, 0.35f) * env * 0.07f;
                });
            }
        }
        return finish(tr, 0.80f);
    }

    // Calm, tense menu theme, 96 bpm, 8 bars
    std::vector<short> makeMenuMusic()
    {
        const float bpm = 96.0f;
        const int step = (int)(SR * 60.0f / bpm / 4.0f);
        const int bars = 8, steps = bars * 16;
        Track tr; tr.buf.assign((size_t)step * steps, 0.0f);
        // Em - C - G - D  (twice)
        const int chords[4][3] = { { -5, -2, 2 }, { -9, -5, -2 }, { -2, 2, 5 }, { -7, -3, 0 } };
        for (int b = 0; b < bars; b++)
        {
            const int* c = chords[b % 4];
            int at = b * 16 * step;
            for (int k = 0; k < 3; k++) pad(tr, at, 16 * step + step * 2, noteHz(c[k] + 12), 0.5f);
            bass(tr, at, step * 6, noteHz(c[0] - 12), 0.55f);
            bass(tr, at + step * 10, step * 4, noteHz(c[0] - 12), 0.40f);
            // arpeggio
            const int order[8] = { 0, 1, 2, 1, 0, 2, 1, 2 };
            for (int st = 0; st < 8; st++)
            {
                int oct = (st >= 4 && b % 2 == 1) ? 36 : 24;
                pluck(tr, at + st * 2 * step, noteHz(c[order[st]] + oct), 0.22f);
                pluck(tr, at + st * 2 * step + step * 3, noteHz(c[order[st]] + oct), 0.07f);
            }
            kick(tr, at, 0.55f);
            kick(tr, at + step * 10, 0.40f);
            for (int st = 0; st < 16; st += 2) hat(tr, at + st * step, st % 4 == 2 ? 0.10f : 0.05f, 60.0f);
            if (b % 2 == 1) snare(tr, at + step * 12, 0.22f);
        }
        return finish(tr, 0.70f);
    }

    void streamCallback(void* data, unsigned int frames)
    {
        short* out = (short*)data;
        int cur = curTrack.load();
        int want = wantTrack.load();
        float target = (cur == want) ? musicGain.load() * (1.0f - duck.load()) : 0.0f;
        for (unsigned int i = 0; i < frames; i++)
        {
            cbGain += (target - cbGain) * 0.00012f;
            if (cur != want && cbGain < 0.004f)
            {
                cur = want; curTrack.store(cur); playPos = 0;
                target = musicGain.load() * (1.0f - duck.load());
            }
            if (!musicReady.load() || hasFileMusic[cur]) { out[i] = 0; continue; }
            const std::vector<short>& l = loops[cur];
            if (l.empty()) { out[i] = 0; continue; }
            if (playPos >= l.size()) playPos = 0;
            out[i] = (short)(l[playPos++] * cbGain);
        }
    }
}

// ---------------------------------------------------------------------------
void audioInit(const std::string& soundDir)
{
    soundFolder = soundDir;
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    ready = true;

    makeEffect(SFX_UI_MOVE, 0.07f, 0.35f, [](float t, float) { return sinf(2 * PI * 880.0f * t) * expf(-t * 60.0f) + 0.3f * sinf(2 * PI * 1760.0f * t) * expf(-t * 90.0f); });
    makeEffect(SFX_UI_SELECT, 0.22f, 0.50f, [](float t, float) {
        float f = t < 0.07f ? 660.0f : 990.0f;
        return (sinf(2 * PI * f * t) + 0.3f * sinf(2 * PI * f * 2 * t)) * expf(-(t < 0.07f ? t : t - 0.07f) * 16.0f); });
    makeEffect(SFX_UI_BACK, 0.16f, 0.40f, [](float t, float) { float f = 520.0f - 900.0f * t; return sinf(2 * PI * f * t) * expf(-t * 22.0f); });

    {
        auto lp = std::make_shared<LP>(); auto lp2 = std::make_shared<LP>();
        makeEffect(SFX_WHOOSH_LIGHT, 0.17f, 0.30f, [lp, lp2](float, float u) {
            float n = frand(); float a = 0.10f + 0.35f * u;
            float band = lp->run(n, a) - lp2->run(n, a * 0.35f);
            return band * sinf(u * PI) * sinf(u * PI); });
    }
    {
        auto lp = std::make_shared<LP>(); auto lp2 = std::make_shared<LP>();
        makeEffect(SFX_WHOOSH_HEAVY, 0.28f, 0.42f, [lp, lp2](float, float u) {
            float n = frand(); float a = 0.05f + 0.22f * u * u;
            float band = lp->run(n, a) - lp2->run(n, a * 0.3f);
            return band * powf(sinf(u * PI), 1.5f); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_HIT_LIGHT, 0.20f, 0.75f, [lp](float t, float) {
            float body = sinf(2 * PI * (70.0f * t + 110.0f / 30.0f * (1 - expf(-t * 30.0f)))) * expf(-t * 22.0f);
            float smack = lp->run(frand(), 0.45f * expf(-t * 30.0f) + 0.03f) * expf(-t * 30.0f);
            return tanhf((body * 0.9f + smack * 1.4f) * 1.8f); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_HIT_HEAVY, 0.42f, 0.95f, [lp](float t, float) {
            float body = sinf(2 * PI * (42.0f * t + 140.0f / 22.0f * (1 - expf(-t * 22.0f)))) * expf(-t * 10.0f);
            float smack = lp->run(frand(), 0.55f * expf(-t * 18.0f) + 0.02f) * expf(-t * 16.0f);
            float crack = frand() * expf(-t * 160.0f);
            return tanhf((body * 1.2f + smack * 1.6f + crack * 0.8f) * 2.2f); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_BLOCK, 0.16f, 0.55f, [lp](float t, float) {
            float n = frand(); float hp = n - lp->run(n, 0.30f);
            return hp * expf(-t * 45.0f) * 0.7f + sinf(2 * PI * 240.0f * t) * expf(-t * 38.0f) * 0.8f + sinf(2 * PI * 1250.0f * t) * expf(-t * 60.0f) * 0.25f; });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_JUMP, 0.16f, 0.22f, [lp](float, float u) { return lp->run(frand(), 0.08f + 0.3f * u) * sinf(u * PI); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_LAND, 0.16f, 0.45f, [lp](float t, float) {
            return sinf(2 * PI * (55.0f * t + 60.0f / 40.0f * (1 - expf(-t * 40.0f)))) * expf(-t * 28.0f) + lp->run(frand(), 0.12f) * expf(-t * 40.0f) * 0.8f; });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_BODY_FALL, 0.36f, 0.70f, [lp](float t, float) {
            return tanhf((sinf(2 * PI * (40.0f * t + 70.0f / 25.0f * (1 - expf(-t * 25.0f)))) * expf(-t * 12.0f) * 1.2f + lp->run(frand(), 0.10f) * expf(-t * 14.0f) * 1.2f) * 1.6f); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_KO, 1.6f, 1.0f, [lp](float t, float) {
            float boom = sinf(2 * PI * (30.0f * t + 110.0f / 6.0f * (1 - expf(-t * 6.0f)))) * expf(-t * 2.6f);
            float crash = lp->run(frand(), 0.5f * expf(-t * 5.0f) + 0.01f) * expf(-t * 3.2f);
            float ring = (sinf(2 * PI * 196.0f * t) + sinf(2 * PI * 293.7f * t) * 0.7f) * expf(-t * 2.2f) * 0.25f;
            return tanhf((boom * 1.3f + crash * 1.1f + ring) * 1.8f); });
    }
    makeEffect(SFX_ROUND, 1.5f, 0.70f, [](float t, float) {
        // bell
        const float f[5] = { 392.0f, 784.5f, 1180.0f, 1571.0f, 2096.0f };
        const float a[5] = { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f };
        float v = 0; for (int i = 0; i < 5; i++) v += a[i] * sinf(2 * PI * f[i] * t) * expf(-t * (2.2f + i * 1.3f));
        return v * std::min(1.0f, t * 500.0f); });
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_FIGHT, 0.75f, 0.85f, [lp](float t, float) {
            float chord = 0; const float f[3] = { 164.8f, 246.9f, 329.6f };
            for (float hz : f) chord += saw(hz * t) + saw(hz * 1.007f * t);
            float c = lp->run(chord, 0.05f + 0.3f * expf(-t * 4.0f)) * expf(-t * 3.5f) * 0.3f;
            float thump = sinf(2 * PI * (50.0f * t + 100.0f / 20.0f * (1 - expf(-t * 20.0f)))) * expf(-t * 9.0f);
            return tanhf((c + thump) * 1.6f); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_VS, 1.1f, 0.85f, [lp](float t, float) {
            float sweep = lp->run(frand(), 0.02f + 0.5f * std::min(1.0f, t / 0.28f)) * (t < 0.28f ? t / 0.28f : expf(-(t - 0.28f) * 6.0f));
            float tt = std::max(0.0f, t - 0.28f);
            float boom = t > 0.28f ? sinf(2 * PI * (38.0f * tt + 120.0f / 12.0f * (1 - expf(-tt * 12.0f)))) * expf(-tt * 4.5f) : 0.0f;
            return tanhf((sweep * 0.7f + boom * 1.4f) * 1.7f); });
    }
    makeEffect(SFX_WIN, 1.5f, 0.60f, [](float t, float) {
        const float notes[5] = { 329.6f, 415.3f, 493.9f, 659.3f, 830.6f };
        float v = 0;
        for (int i = 0; i < 5; i++)
        {
            float tt = t - i * 0.11f;
            if (tt < 0) continue;
            float d = (i == 4) ? 1.6f : 5.0f;
            v += (sqr(notes[i] * tt, 0.4f) * 0.5f + sinf(2 * PI * notes[i] * tt)) * expf(-tt * d) * std::min(1.0f, tt * 300.0f);
        }
        return v * 0.4f; });


    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_SPECIAL, 0.34f, 0.55f, [lp](float t, float u) {
            float f = 240.0f + 900.0f * u * u;
            return (sinf(2 * PI * f * t) * 0.5f + lp->run(frand(), 0.1f + 0.4f * u) * 0.8f) * sinf(u * PI); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_CHARGE, 0.85f, 0.80f, [lp](float t, float u) {
            float f = 90.0f + 520.0f * u * u;
            float trem = 0.7f + 0.3f * sinf(2 * PI * (14.0f + 30.0f * u) * t);
            float tone = saw(f * t) * 0.35f + sinf(2 * PI * f * 2.0f * t) * 0.35f + sinf(2 * PI * f * 3.01f * t) * 0.2f;
            return (lp->run(tone, 0.06f + 0.3f * u) * trem + lp->run(frand(), 0.2f) * 0.15f * u) * std::min(1.0f, u * 6.0f); });
    }
    {
        auto lp = std::make_shared<LP>(); auto lp2 = std::make_shared<LP>();
        makeEffect(SFX_BEAM, 1.15f, 1.0f, [lp, lp2](float t, float u) {
            float env = std::min(1.0f, t * 40.0f) * std::min(1.0f, (1.0f - u) * 5.0f);
            float roar = lp->run(frand(), 0.16f) * 1.4f;
            float hum = saw(72.0f * t) * 0.5f + saw(108.5f * t) * 0.4f + sinf(2 * PI * 36.0f * t);
            float sizzle = (frand() - lp2->run(frand(), 0.4f)) * 0.25f;
            float wob = 0.8f + 0.2f * sinf(2 * PI * 23.0f * t);
            return tanhf((roar + hum * 0.6f) * wob * 1.5f + sizzle) * env; });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_SHOT, 0.42f, 0.80f, [lp](float t, float u) {
            float f = 620.0f * expf(-t * 7.0f) + 120.0f;
            float zap = sinf(2 * PI * (120.0f * t + 620.0f / 7.0f * (1 - expf(-t * 7.0f)))) * expf(-t * 6.0f);
            (void)f;
            float air = lp->run(frand(), 0.35f * expf(-t * 5.0f) + 0.03f) * expf(-t * 5.0f);
            return tanhf((zap * 0.9f + air * 1.3f) * 1.6f) * std::min(1.0f, (1.0f - u) * 6.0f); });
    }
    {
        auto lp = std::make_shared<LP>();
        makeEffect(SFX_EXPLODE, 0.75f, 1.0f, [lp](float t, float) {
            float boom = sinf(2 * PI * (34.0f * t + 150.0f / 14.0f * (1 - expf(-t * 14.0f)))) * expf(-t * 6.0f);
            float crash = lp->run(frand(), 0.6f * expf(-t * 9.0f) + 0.02f) * expf(-t * 7.0f);
            return tanhf((boom * 1.3f + crash * 1.5f) * 2.0f); });
    }

    // music files (optional) replace the generated loops
    {
        const char* names[3] = { "", "music_menu", "music_fight" };
        for (int t = 1; t < 3; t++)
        {
            std::string file = findSoundFile(names[t]);
            if (file.empty()) continue;
            fileMusic[t] = LoadMusicStream(file.c_str());
            hasFileMusic[t] = IsMusicValid(fileMusic[t]);
            if (hasFileMusic[t]) fileMusic[t].looping = true;
        }
    }

    // the music takes a moment to render, so do it in the background
    musicThread = std::thread([]() {
        if (!hasFileMusic[MUSIC_MENU]) loops[MUSIC_MENU] = makeMenuMusic();
        if (!hasFileMusic[MUSIC_FIGHT]) loops[MUSIC_FIGHT] = makeFightMusic();
        musicReady = true;
    });
    SetAudioStreamBufferSizeDefault(2048);
    stream = LoadAudioStream(SR, 16, 1);
    SetAudioStreamCallback(stream, streamCallback);
    musicGain = 0.42f;
    PlayAudioStream(stream);
}

void audioShutdown()
{
    if (!ready) return;
    if (musicThread.joinable()) musicThread.join();
    for (int t = 1; t < 3; t++) if (hasFileMusic[t]) UnloadMusicStream(fileMusic[t]);
    UnloadAudioStream(stream);
    for (Effect& e : effects)
    {
        if (!e.ok) continue;
        for (int k = 1; k < VOICES; k++) UnloadSoundAlias(e.s[k]);
        UnloadSound(e.s[0]);
    }
    CloseAudioDevice();
    ready = false;
}

// streams music files (when present) and cross-fades between tracks
void audioUpdate(float dt)
{
    if (!ready) return;
    int want = wantTrack.load();
    bool wantFile = want != MUSIC_NONE && hasFileMusic[want];
    float target = (wantFile && fileTrack == want && !muted) ? 0.55f * (1.0f - duck.load()) : 0.0f;
    fileGain += clampf(target - fileGain, -dt * 2.5f, dt * 2.5f);
    if (fileTrack != MUSIC_NONE && fileTrack != want && fileGain <= 0.001f) { StopMusicStream(fileMusic[fileTrack]); fileTrack = MUSIC_NONE; }
    if (wantFile && fileTrack == MUSIC_NONE) { fileTrack = want; PlayMusicStream(fileMusic[fileTrack]); }
    if (fileTrack != MUSIC_NONE)
    {
        SetMusicVolume(fileMusic[fileTrack], fileGain);
        UpdateMusicStream(fileMusic[fileTrack]);
    }
}

void sfxPlay(Sfx id, float volume, float pitch, float pan)
{
    if (!ready || muted || !effects[id].ok) return;
    Effect& e = effects[id];
    Sound s = e.s[e.next];
    e.next = (e.next + 1) % VOICES;
    SetSoundVolume(s, volume);
    SetSoundPitch(s, pitch);
    SetSoundPan(s, pan);
    PlaySound(s);
}

void musicPlay(MusicTrack t) { wantTrack = (int)t; }
void musicDuck(float amount) { duck = clampf(amount, 0.0f, 1.0f); }
void audioToggleMute()
{
    muted = !muted;
    musicGain = muted ? 0.0f : 0.42f;
}
bool audioMuted() { return muted; }
