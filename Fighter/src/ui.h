// ============================================================================
//  TUF FIGHTER - ui.h
//  2D drawing helpers. Everything is laid out on a virtual canvas that is
//  1080 units high (1920 wide on a 16:9 screen) and scaled to the window.
// ============================================================================
#pragma once
#include "engine.h"

enum Align { AL_LEFT, AL_CENTER, AL_RIGHT };

struct UI
{
    Font title{};     // black italic - logos, names, announcements
    Font heavy{};     // extra bold italic - HUD
    Font body{};      // semi bold - menus, descriptions
    float s = 1.0f;   // pixels per virtual unit
    float W = 1920.0f, H = 1080.0f;
    Texture2D vignette{};
    Texture2D glow{};

    bool load(const std::string& fontDir);
    void unload();
    void beginFrame();                                        // refresh the scale for the current window size

    float measure(const Font& f, const char* txt, float size, float spacing = 0) const;
    void text(const Font& f, const char* txt, float x, float y, float size, Color c, Align a = AL_LEFT, float spacing = 0) const;
    void textShadow(const Font& f, const char* txt, float x, float y, float size, Color c, Align a = AL_LEFT, float off = 4, Color sh = { 0, 0, 0, 200 }, float spacing = 0) const;
    void textOutline(const Font& f, const char* txt, float x, float y, float size, Color c, Color outline, float thick, Align a = AL_LEFT, float spacing = 0) const;
    // text scaled around its centre (announcements)
    void textPop(const Font& f, const char* txt, float cx, float cy, float size, Color c, Color outline, float thick, float spacing = 0) const;

    void rect(float x, float y, float w, float h, Color c) const;
    void rectGradV(float x, float y, float w, float h, Color top, Color bottom) const;
    void rectGradH(float x, float y, float w, float h, Color left, Color right) const;
    // parallelogram: the top edge is shifted right by `skew`
    void para(float x, float y, float w, float h, float skew, Color c) const;
    void paraGradH(float x, float y, float w, float h, float skew, Color left, Color right) const;
    void paraGradV(float x, float y, float w, float h, float skew, Color top, Color bottom) const;
    void paraLines(float x, float y, float w, float h, float skew, float thick, Color c) const;
    void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color ctl, Color ctr, Color cbr, Color cbl) const;
    void line(float x0, float y0, float x1, float y1, float thick, Color c) const;
    void circle(float x, float y, float r, Color c) const;
    void ring(float x, float y, float r0, float r1, Color c) const;
    void diamond(float cx, float cy, float r, Color c) const;
    void glowAt(float x, float y, float r, Color c) const;    // soft additive blob
    void drawVignette(float strength) const;
    void fade(float alpha, Color c = BLACK) const;            // full screen fade
    void texture(Texture2D t, Rectangle src, float x, float y, float w, float h, Color tint) const;
};
