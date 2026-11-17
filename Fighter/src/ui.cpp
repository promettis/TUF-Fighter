// ============================================================================
//  TUF FIGHTER - ui.cpp
// ============================================================================
#include "ui.h"

static Font loadFont(const std::string& path, int size)
{
    Font f = LoadFontEx(path.c_str(), size, nullptr, 0);
    if (f.texture.id == 0) return GetFontDefault();
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

bool UI::load(const std::string& dir)
{
    title = loadFont(dir + "/BarlowCondensed-BlackItalic.ttf", 150);
    heavy = loadFont(dir + "/BarlowCondensed-ExtraBoldItalic.ttf", 96);
    body = loadFont(dir + "/BarlowCondensed-SemiBold.ttf", 72);

    Image v = GenImageGradientRadial(512, 512, 0.25f, Color{ 0, 0, 0, 0 }, Color{ 0, 0, 0, 255 });
    vignette = LoadTextureFromImage(v);
    SetTextureFilter(vignette, TEXTURE_FILTER_BILINEAR);
    UnloadImage(v);
    Image g = GenImageGradientRadial(128, 128, 0.0f, WHITE, Color{ 255, 255, 255, 0 });
    glow = LoadTextureFromImage(g);
    SetTextureFilter(glow, TEXTURE_FILTER_BILINEAR);
    UnloadImage(g);
    return true;
}

void UI::unload()
{
    if (title.texture.id != GetFontDefault().texture.id) UnloadFont(title);
    if (heavy.texture.id != GetFontDefault().texture.id) UnloadFont(heavy);
    if (body.texture.id != GetFontDefault().texture.id) UnloadFont(body);
    UnloadTexture(vignette);
    UnloadTexture(glow);
}

void UI::beginFrame()
{
    s = GetScreenHeight() / 1080.0f;
    H = 1080.0f;
    W = GetScreenWidth() / s;
}

float UI::measure(const Font& f, const char* txt, float size, float spacing) const
{
    return MeasureTextEx(f, txt, size, spacing).x;
}

void UI::text(const Font& f, const char* txt, float x, float y, float size, Color c, Align a, float spacing) const
{
    if (a != AL_LEFT)
    {
        float w = measure(f, txt, size, spacing);
        x -= (a == AL_CENTER) ? w * 0.5f : w;
    }
    DrawTextEx(f, txt, Vector2{ x * s, y * s }, size * s, spacing * s, c);
}

void UI::textShadow(const Font& f, const char* txt, float x, float y, float size, Color c, Align a, float off, Color sh, float spacing) const
{
    sh.a = (unsigned char)(sh.a * (c.a / 255.0f));
    text(f, txt, x + off, y + off, size, sh, a, spacing);
    text(f, txt, x, y, size, c, a, spacing);
}

void UI::textOutline(const Font& f, const char* txt, float x, float y, float size, Color c, Color outline, float thick, Align a, float spacing) const
{
    outline.a = (unsigned char)(outline.a * (c.a / 255.0f));
    for (int i = 0; i < 8; i++)
    {
        float ang = i * PI / 4.0f;
        text(f, txt, x + cosf(ang) * thick, y + sinf(ang) * thick, size, outline, a, spacing);
    }
    text(f, txt, x + thick * 0.6f, y + thick * 1.6f, size, outline, a, spacing);
    text(f, txt, x, y, size, c, a, spacing);
}

void UI::textPop(const Font& f, const char* txt, float cx, float cy, float size, Color c, Color outline, float thick, float spacing) const
{
    textOutline(f, txt, cx, cy - size * 0.5f, size, c, outline, thick, AL_CENTER, spacing);
}

void UI::quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color ctl, Color ctr, Color cbr, Color cbl) const
{
    rlBegin(RL_TRIANGLES);
    rlColor4ub(ctl.r, ctl.g, ctl.b, ctl.a); rlVertex2f(tl.x * s, tl.y * s);
    rlColor4ub(cbl.r, cbl.g, cbl.b, cbl.a); rlVertex2f(bl.x * s, bl.y * s);
    rlColor4ub(cbr.r, cbr.g, cbr.b, cbr.a); rlVertex2f(br.x * s, br.y * s);
    rlColor4ub(ctl.r, ctl.g, ctl.b, ctl.a); rlVertex2f(tl.x * s, tl.y * s);
    rlColor4ub(cbr.r, cbr.g, cbr.b, cbr.a); rlVertex2f(br.x * s, br.y * s);
    rlColor4ub(ctr.r, ctr.g, ctr.b, ctr.a); rlVertex2f(tr.x * s, tr.y * s);
    rlEnd();
}

void UI::rect(float x, float y, float w, float h, Color c) const
{
    quad({ x, y }, { x + w, y }, { x + w, y + h }, { x, y + h }, c, c, c, c);
}
void UI::rectGradV(float x, float y, float w, float h, Color top, Color bottom) const
{
    quad({ x, y }, { x + w, y }, { x + w, y + h }, { x, y + h }, top, top, bottom, bottom);
}
void UI::rectGradH(float x, float y, float w, float h, Color left, Color right) const
{
    quad({ x, y }, { x + w, y }, { x + w, y + h }, { x, y + h }, left, right, right, left);
}
void UI::para(float x, float y, float w, float h, float skew, Color c) const
{
    quad({ x + skew, y }, { x + w + skew, y }, { x + w, y + h }, { x, y + h }, c, c, c, c);
}
void UI::paraGradH(float x, float y, float w, float h, float skew, Color left, Color right) const
{
    quad({ x + skew, y }, { x + w + skew, y }, { x + w, y + h }, { x, y + h }, left, right, right, left);
}
void UI::paraGradV(float x, float y, float w, float h, float skew, Color top, Color bottom) const
{
    quad({ x + skew, y }, { x + w + skew, y }, { x + w, y + h }, { x, y + h }, top, top, bottom, bottom);
}
void UI::paraLines(float x, float y, float w, float h, float skew, float t, Color c) const
{
    line(x + skew, y, x + w + skew, y, t, c);
    line(x + w + skew, y, x + w, y + h, t, c);
    line(x + w, y + h, x, y + h, t, c);
    line(x, y + h, x + skew, y, t, c);
}
void UI::line(float x0, float y0, float x1, float y1, float thick, Color c) const
{
    DrawLineEx(Vector2{ x0 * s, y0 * s }, Vector2{ x1 * s, y1 * s }, std::max(1.0f, thick * s), c);
}
void UI::circle(float x, float y, float r, Color c) const { DrawCircleV(Vector2{ x * s, y * s }, r * s, c); }
void UI::ring(float x, float y, float r0, float r1, Color c) const
{
    DrawRing(Vector2{ x * s, y * s }, r0 * s, r1 * s, 0, 360, 48, c);
}
void UI::diamond(float cx, float cy, float r, Color c) const
{
    quad({ cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy }, c, c, c, c);
}
void UI::glowAt(float x, float y, float r, Color c) const
{
    BeginBlendMode(BLEND_ADDITIVE);
    DrawTexturePro(glow, Rectangle{ 0, 0, (float)glow.width, (float)glow.height },
                   Rectangle{ (x - r) * s, (y - r) * s, 2 * r * s, 2 * r * s }, Vector2{ 0, 0 }, 0, c);
    EndBlendMode();
}
void UI::drawVignette(float strength) const
{
    DrawTexturePro(vignette, Rectangle{ 0, 0, (float)vignette.width, (float)vignette.height },
                   Rectangle{ -W * 0.18f * s, -H * 0.18f * s, W * 1.36f * s, H * 1.36f * s }, Vector2{ 0, 0 }, 0,
                   Color{ 255, 255, 255, (unsigned char)(saturate(strength) * 255) });
}
void UI::fade(float alpha, Color c) const
{
    if (alpha <= 0.0f) return;
    c.a = (unsigned char)(saturate(alpha) * 255);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), c);
}
void UI::texture(Texture2D t, Rectangle src, float x, float y, float w, float h, Color tint) const
{
    DrawTexturePro(t, src, Rectangle{ x * s, y * s, w * s, h * s }, Vector2{ 0, 0 }, 0, tint);
}
