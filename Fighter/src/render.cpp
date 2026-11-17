// ============================================================================
//  TUF FIGHTER - render.cpp
// ============================================================================
#include "render.h"

constexpr int MAX_BONES = 100;
constexpr int SHADOW_SIZE = 2048;
constexpr int SHADOW_SLOT = 10;

// ---------------------------------------------------------------------------
// Shaders (GLSL 330)
// ---------------------------------------------------------------------------
static const char* VS_LIT = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 lightVP;
uniform int skinned;
uniform mat4 boneMatrices[100];
out vec3 fragPos;
out vec2 fragUV;
out vec3 fragN;
out vec4 fragShadow;
void main()
{
    vec4 p = vec4(vertexPosition, 1.0);
    vec3 n = vertexNormal;
    if (skinned == 1)
    {
        mat4 sk = vertexBoneWeights.x*boneMatrices[int(vertexBoneIndices.x)]
                + vertexBoneWeights.y*boneMatrices[int(vertexBoneIndices.y)]
                + vertexBoneWeights.z*boneMatrices[int(vertexBoneIndices.z)]
                + vertexBoneWeights.w*boneMatrices[int(vertexBoneIndices.w)];
        p = sk*p;
        n = mat3(sk)*n;
    }
    vec4 wp = matModel*p;
    fragPos = wp.xyz;
    fragN = mat3(matModel)*n;
    fragUV = vertexTexCoord;
    fragShadow = lightVP*wp;
    gl_Position = mvp*p;
}
)";

static const char* FS_COMMON = R"(#version 330
in vec3 fragPos;
in vec2 fragUV;
in vec3 fragN;
in vec4 fragShadow;
uniform sampler2D texture0;
uniform sampler2D texture2;
uniform sampler2D texORM;
uniform sampler2D shadowMap;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 skyColor;
uniform vec3 groundColor;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float exposure;
uniform float shadowOn;
uniform vec4 flashColor;
uniform vec4 rimColor;
uniform float litAmount;
uniform float useNormalMap;
uniform float useORM;
uniform float saturation;
out vec4 finalColor;

float shadowFactor(vec3 N)
{
    if (shadowOn < 0.5) return 1.0;
    vec3 p = fragShadow.xyz/fragShadow.w*0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    float bias = max(0.0022*(1.0 - dot(N, sunDir)), 0.0007);
    vec2 texel = 1.0/vec2(textureSize(shadowMap, 0));
    float s = 0.0;
    for (int x = -2; x <= 2; x++)
        for (int y = -2; y <= 2; y++)
            s += (p.z - bias > texture(shadowMap, p.xy + vec2(x, y)*texel).r) ? 0.0 : 1.0;
    // fade the shadow out at the border of the shadow map
    vec2 e = smoothstep(vec2(0.0), vec2(0.06), p.xy)*smoothstep(vec2(0.0), vec2(0.06), 1.0 - p.xy);
    return mix(1.0, s/25.0, e.x*e.y);
}

vec3 tonemap(vec3 x)
{
    x *= exposure;
    x = (x*(2.51*x + 0.03))/(x*(2.43*x + 0.59) + 0.14);
    return pow(clamp(x, 0.0, 1.0), vec3(1.0/2.2));
}

vec3 applyFog(vec3 c)
{
    float d = length(fragPos - viewPos);
    float f = 1.0 - exp(-pow(d*fogDensity, 1.6));
    return mix(c, fogColor, clamp(f, 0.0, 1.0));
}
)";

static const char* FS_LIT_MAIN = R"(
vec3 perturbNormal(vec3 N)
{
    vec3 dp1 = dFdx(fragPos), dp2 = dFdy(fragPos);
    vec2 duv1 = dFdx(fragUV), duv2 = dFdy(fragUV);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp*duv1.x + dp1perp*duv2.x;
    vec3 B = dp2perp*duv1.y + dp1perp*duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
    vec3 m = texture(texture2, fragUV).xyz*2.0 - 1.0;
    m.xy *= 0.8;
    return normalize(mat3(T*invmax, B*invmax, N)*m);
}

void main()
{
    vec4 tex = texture(texture0, fragUV);
    vec3 albedo = pow(tex.rgb, vec3(2.2))*colDiffuse.rgb;
    vec3 N = normalize(fragN);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(viewPos - fragPos);
    vec3 Ng = N;
    if (useNormalMap > 0.5) N = perturbNormal(N);

    float rough = 0.75;
    if (useORM > 0.5) rough = clamp(texture(texORM, fragUV).g, 0.25, 1.0);

    float sh = shadowFactor(Ng);
    float ndl = dot(N, sunDir);
    float diff = clamp(ndl*0.85 + 0.15, 0.0, 1.0)*sh;          // slightly wrapped diffuse
    vec3 amb = mix(groundColor, skyColor, N.y*0.5 + 0.5);
    vec3 H = normalize(sunDir + V);
    float shin = mix(96.0, 6.0, rough);
    float spec = pow(max(dot(N, H), 0.0), shin)*(1.0 - rough)*0.55*step(0.0, ndl)*sh;
    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);

    vec3 lighting = amb + sunColor*diff;
    vec3 c = albedo*mix(vec3(1.0), lighting, litAmount);
    c += sunColor*spec*litAmount;
    c += rimColor.rgb*rimColor.a*fres*(0.35 + 0.65*clamp(N.y*0.5 + 0.6, 0.0, 1.0));
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(g), c, saturation);
    c = applyFog(c);
    c = tonemap(c);
    c = mix(c, flashColor.rgb, flashColor.a);
    finalColor = vec4(c, colDiffuse.a);
}
)";

static const char* FS_GROUND_MAIN = R"(
uniform vec4 arena;      // x = half width, y = half depth, z = z offset
uniform float time;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p)
{
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++) { s += a*noise(p); p = p*2.03 + 17.0; a *= 0.5; }
    return s;
}
float sdBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 w = fragPos.xz;
    float dist = length(fragPos - viewPos);

    // --- paving stones ---
    vec2 tc = w/1.6;
    tc.x += 0.5*floor(tc.y);                       // running bond
    vec2 id = floor(tc), f = fract(tc);
    vec2 fw = fwidth(tc);
    vec2 e = smoothstep(vec2(0.0), fw*1.5 + 0.012, f)*smoothstep(vec2(0.0), fw*1.5 + 0.012, 1.0 - f);
    float detail = clamp(1.0 - max(fw.x, fw.y)*3.0, 0.0, 1.0);
    float joint = (1.0 - e.x*e.y)*detail;
    float var = (hash(id) - 0.5)*0.10*detail;
    float grain = fbm(w*2.2)*0.5 + fbm(w*11.0)*0.25*detail;
    vec3 stone = vec3(0.70, 0.65, 0.56)*(0.80 + var + grain*0.40);
    stone = mix(stone, vec3(0.30, 0.28, 0.25), joint*0.80);
    float lawn = 0.0;
    if (arena.w > 0.5)
    {
        // ---- campus layout: walkway, lawns, asphalt drive, chequered forecourt ----
        float ax = abs(w.x), z = w.y;
        float aaz = fwidth(z)*1.2 + 0.01, aax = fwidth(ax)*1.2 + 0.01;
        // forecourt in front of the steps
        float fc = (1.0 - smoothstep(-aaz, aaz, abs(z + 44.25) - 9.25))*(1.0 - smoothstep(-aax, aax, ax - 16.5));
        vec2 ct = floor(w/1.25);
        float chk = mod(ct.x + ct.y, 2.0);
        vec2 cf = abs(fract(w/1.25) - 0.5);
        float cj = smoothstep(0.47, 0.5, max(cf.x, cf.y))*detail;
        vec3 cobble = mix(vec3(0.83, 0.81, 0.74), vec3(0.66, 0.65, 0.61), chk*clamp(detail*1.5, 0.0, 1.0)*0.9 + 0.05)*(0.90 + 0.18*fbm(w*2.0));
        cobble = mix(cobble, vec3(0.40, 0.38, 0.35), cj*0.6);
        stone = mix(stone, cobble, fc);
        // asphalt drive
        float dr = abs(z + 27.25) - 7.25;
        float asph = 1.0 - smoothstep(-aaz, aaz, dr);
        vec3 tar = vec3(0.235, 0.235, 0.245)*(0.82 + 0.30*fbm(w*1.7) + 0.20*noise(w*40.0)*detail);
        float dash = (1.0 - smoothstep(0.07, 0.07 + aaz, abs(z + 27.25)))*step(0.5, fract(w.x/7.0));
        tar = mix(tar, vec3(0.82, 0.80, 0.72), dash*0.8);
        stone = mix(stone, tar, asph);
        stone = mix(stone, vec3(0.80, 0.78, 0.72), (1.0 - smoothstep(0.12, 0.12 + aaz, abs(dr)))*0.85);
        // lawns: both sides of the walkway, both sides of the forecourt and around the building
        float dA = sdBox(vec2(ax - 40.0, z + 13.4), vec2(34.4, 5.6), 0.8);
        float dB = sdBox(vec2(ax - 48.5, z + 70.0), vec2(31.5, 34.5), 0.8);
        float dl = min(dA, dB);
        float al = fwidth(dl)*1.2 + 0.01;
        lawn = 1.0 - smoothstep(-al, al, dl);
        lawn = max(lawn, max(smoothstep(81.0, 82.0, ax), smoothstep(-105.5, -106.5, z)));
        vec3 grass = vec3(0.27, 0.40, 0.16)*(0.72 + 0.50*fbm(w*0.33));
        grass *= 0.94 + 0.06*sin(w.x*1.5 + w.y*0.35);
        grass *= 0.86 + 0.28*noise(w*38.0)*detail;
        grass = mix(grass, vec3(0.42, 0.46, 0.24), smoothstep(84.0, 150.0, max(ax, -z))*0.6);     // drier fields far away
        stone = mix(stone, grass, lawn);
        stone = mix(stone, vec3(0.78, 0.76, 0.70), (1.0 - smoothstep(0.10, 0.10 + al, abs(dl - 0.12)))*0.9);   // kerb stones
    }

    // --- painted fighting court ---
    vec2 cp = vec2(w.x, w.y - arena.z);
    float d = sdBox(cp, arena.xy, 0.6);
    float aa = fwidth(d)*1.2 + 0.002;
    float inside = 1.0 - smoothstep(-aa, aa, d);
    float wear = 0.82 + 0.30*fbm(w*1.3) - 0.25*joint;
    vec3 court = mix(vec3(0.20, 0.035, 0.045), vec3(0.34, 0.06, 0.065), fbm(w*0.7))*wear;
    stone = mix(stone, mix(stone, court, 0.90), inside);
    // border lines
    float line = 1.0 - smoothstep(0.045, 0.045 + aa, abs(d + 0.30));
    float outer = 1.0 - smoothstep(0.07, 0.07 + aa, abs(d));
    // centre line and start marks
    float centre = (1.0 - smoothstep(0.04, 0.04 + aa, abs(cp.x)))*inside*step(d, -0.30);
    float marks = (1.0 - smoothstep(0.04, 0.04 + aa, abs(abs(cp.x) - 1.55)))*(1.0 - smoothstep(0.55, 0.55 + aa, abs(cp.y)));
    float paint = max(max(line, outer), max(centre*0.6, marks))*(0.75 + 0.25*fbm(w*4.0));
    stone = mix(stone, vec3(0.86, 0.84, 0.78), paint*0.92);

    vec3 albedo = pow(stone, vec3(2.2))*colDiffuse.rgb;
    vec3 N = normalize(vec3((noise(w*9.0) - 0.5)*0.06*detail, 1.0, (noise(w*9.0 + 31.0) - 0.5)*0.06*detail));
    vec3 V = normalize(viewPos - fragPos);
    float sh = shadowFactor(vec3(0.0, 1.0, 0.0));
    float diff = max(dot(N, sunDir), 0.0)*sh;
    vec3 amb = skyColor*0.95;
    vec3 H = normalize(sunDir + V);
    float spec = pow(max(dot(N, H), 0.0), 40.0)*0.20*sh*(1.0 - joint)*(1.0 - lawn);
    float fres = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 c = albedo*(amb + sunColor*diff) + sunColor*spec + fogColor*fres*0.10;
    c = applyFog(c);
    finalColor = vec4(tonemap(c), 1.0);
}
)";

// Procedural materials for the university building stage. Everything is shaded
// from the world position, so the geometry needs no texture coordinates.
static const char* FS_BLOC_MAIN = R"(
uniform int matId;
uniform vec4 signRect;      // x0, y0, x1, y1 of the lettering on the facade
uniform vec3 zenithColor;
uniform float glassTop;
uniform float time;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p)
{
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++) { s += a*noise(p); p = p*2.02 + 11.0; a *= 0.5; }
    return s;
}

// what a reflective surface "sees" in direction d
vec3 envColor(vec3 d, float clouds)
{
    if (d.y < 0.0)
    {
        vec3 g = mix(vec3(0.40, 0.38, 0.33), vec3(0.10, 0.17, 0.07), smoothstep(0.40, 0.62, noise(d.xz*5.0 + 3.0)));
        return mix(fogColor, g, clamp(-d.y*7.0, 0.0, 1.0));
    }
    vec3 c = mix(fogColor, zenithColor, pow(clamp(d.y, 0.0, 1.0), 0.5));
    if (clouds > 0.0)
    {
        vec2 uv = d.xz/(d.y + 0.12)*0.9 + vec2(time*0.006, time*0.002);
        float n = fbm(uv) + 0.35*fbm(uv*3.1 + 5.0);
        c = mix(c, vec3(1.15, 1.15, 1.18), smoothstep(0.62, 1.02, n)*smoothstep(0.0, 0.16, d.y)*0.85*clouds);
    }
    // a band of trees / buildings along the horizon
    c = mix(c, vec3(0.16, 0.22, 0.12), smoothstep(0.05, 0.0, d.y)*0.75);
    return c;
}

void main()
{
    vec3 N = normalize(fragN);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(viewPos - fragPos);
    float sh = shadowFactor(N);
    vec3 amb = mix(groundColor, skyColor, N.y*0.5 + 0.5);
    float ndl = max(dot(N, sunDir), 0.0)*sh;
    vec3 c;

    if (matId == 0)
    {
        // ---- white cladding panels ----
        vec2 p = abs(N.z) > 0.6 ? fragPos.xy : (abs(N.x) > 0.6 ? fragPos.zy : fragPos.xz);
        vec2 q = p/vec2(1.92, 2.58);
        vec2 id = floor(q), f = fract(q);
        vec2 fw = fwidth(q);
        vec2 e = smoothstep(vec2(0.0), fw*1.3 + 0.004, f)*smoothstep(vec2(0.0), fw*1.3 + 0.004, 1.0 - f);
        float seam = (1.0 - e.x*e.y)*clamp(1.25 - max(fw.x, fw.y)*7.0, 0.30, 1.0);
        vec3 base = pow(vec3(0.925, 0.935, 0.955), vec3(2.2))*(1.0 + (hash(id) - 0.5)*0.045);
        float ao = mix(0.74, 1.0, smoothstep(0.0, 3.2, fragPos.y));
        c = base*(amb*ao + sunColor*ndl);
        c *= 0.955 + 0.07*noise(p*0.21 + 4.0);
        c *= 1.0 - 0.42*seam;
        // slightly glossy metal: a soft, blurred reflection of the surroundings
        vec3 R = reflect(-V, N);
        R = normalize(R + 0.22*vec3(noise(p*0.13) - 0.5, noise(p*0.13 + 9.0) - 0.5, 0.0));
        float F = 0.05 + 0.55*pow(1.0 - max(dot(N, V), 0.0), 4.0);
        c = mix(c, envColor(R, 0.0), F*0.5*(1.0 - seam));

        // ---- gold lettering on the front ----
        if (N.z > 0.6)
        {
            vec2 t = (fragPos.xy - signRect.xy)/(signRect.zw - signRect.xy);
            if (t.x > 0.0 && t.x < 1.0 && t.y > 0.0 && t.y < 1.0)
            {
                float a = clamp(texture(texture0, vec2(t.x, 1.0 - t.y)).a*1.7, 0.0, 1.0);    // slightly emboldened so it stays readable from afar
                float shd = texture(texture0, vec2(t.x - 0.0022, 1.0 - t.y - 0.045)).a;
                c *= 1.0 - 0.45*shd*(1.0 - a);
                float hi = texture(texture0, vec2(t.x + 0.0012, 1.0 - t.y + 0.03)).a;      // bevel highlight
                vec3 gold = pow(mix(vec3(0.50, 0.31, 0.07), vec3(0.86, 0.62, 0.20), clamp(0.35 + 0.5*t.y + 0.5*(a - hi), 0.0, 1.0)), vec3(2.2));
                vec3 lit = gold*(amb + sunColor*(ndl*0.9 + 0.25)) + sunColor*vec3(1.0, 0.85, 0.5)*0.22*(a - hi);
                c = mix(c, lit, a);
            }
        }
    }
    else if (matId == 1)
    {
        // ---- dark reflective curtain wall ----
        vec2 q = fragPos.xy/vec2(1.50, 1.40);
        vec2 id = floor(q), f = fract(q), fw = fwidth(q);
        vec2 e = smoothstep(vec2(0.0), fw*1.2 + 0.020, f)*smoothstep(vec2(0.0), fw*1.2 + 0.020, 1.0 - f);
        float frame = 1.0 - e.x*e.y;
        // every pane is tilted a tiny bit, which breaks the reflection up like real glazing
        vec3 Np = normalize(N + vec3(hash(id) - 0.5, hash(id + 7.0) - 0.5, 0.0)*0.075);
        vec3 R = reflect(-V, Np);
        float F = 0.13 + 0.80*pow(1.0 - max(dot(Np, V), 0.0), 5.0);
        vec3 inside = vec3(0.006, 0.008, 0.011) + vec3(1.0, 0.78, 0.45)*0.035*step(0.92, hash(id + 3.0));
        c = mix(inside, envColor(R, 1.0)*0.70, F);
        c *= mix(0.40, 1.0, smoothstep(0.0, 2.6, glassTop - fragPos.y));          // shadow of the recess
        c = mix(c, vec3(0.010, 0.011, 0.013), frame*0.92);
        float glint = pow(max(dot(reflect(-sunDir, Np), V), 0.0), 220.0);
        c += sunColor*glint*2.0*(1.0 - frame);
    }
    else if (matId == 2)
    {
        // ---- dark granite steps ----
        float sp = noise(fragPos.xz*23.0 + fragPos.y*11.0);
        vec3 base = vec3(0.050, 0.052, 0.058)*(0.75 + 0.55*sp);
        vec3 H = normalize(sunDir + V);
        c = base*(amb + sunColor*ndl) + sunColor*pow(max(dot(N, H), 0.0), 60.0)*0.12*sh;
        c += envColor(reflect(-V, N), 0.0)*0.06*pow(1.0 - max(dot(N, V), 0.0), 3.0);
    }
    else if (matId == 5)
    {
        // ---- leaves ----
        float mott = 0.55 + 0.55*noise(fragPos.xz*1.9 + fragPos.y*1.5) + 0.25*noise(fragPos.xz*7.0 + fragPos.y*6.0);
        vec3 base = pow(colDiffuse.rgb, vec3(2.2))*mott;
        float wrap = clamp(dot(N, sunDir)*0.65 + 0.35, 0.0, 1.0);
        float back = pow(max(dot(-V, sunDir), 0.0), 3.0)*0.25;                    // light shining through
        c = base*(amb*0.70 + sunColor*(wrap*0.85 + back));
    }
    else if (matId == 6)
    {
        // ---- brick boundary walls with a stone coping ----
        vec2 p = abs(N.z) > 0.6 ? fragPos.xy : (abs(N.x) > 0.6 ? fragPos.zy : fragPos.xz);
        vec2 q = p/vec2(0.46, 0.15);
        q.x += 0.5*floor(q.y);
        vec2 id = floor(q), f = fract(q), fw = fwidth(q);
        float det = clamp(1.0 - max(fw.x, fw.y)*1.3, 0.0, 1.0);
        vec2 e = smoothstep(vec2(0.0), fw + vec2(0.04, 0.10), f)*smoothstep(vec2(0.0), fw + vec2(0.04, 0.10), 1.0 - f);
        float mortar = (1.0 - e.x*e.y)*det;
        vec3 brick = pow(colDiffuse.rgb, vec3(2.2))*(0.80 + 0.40*hash(id)*det + 0.16*noise(p*1.3));
        brick = mix(brick, vec3(0.42, 0.39, 0.34), mortar*0.75);
        if (N.y > 0.6) brick = vec3(0.50, 0.47, 0.42);
        c = brick*(amb + sunColor*ndl);
    }
    else if (matId == 7)
    {
        // ---- neighbouring buildings: painted plaster with rows of windows ----
        vec3 base = pow(colDiffuse.rgb, vec3(2.2));
        if (N.y > 0.6) c = vec3(0.26, 0.26, 0.27)*(amb + sunColor*ndl);
        else
        {
            vec2 p = abs(N.z) > 0.6 ? fragPos.xy : fragPos.zy;
            vec2 q = p/vec2(3.4, 3.3);
            vec2 id = floor(q), f = fract(q), fw = fwidth(q);
            vec2 w0 = smoothstep(vec2(0.20, 0.30) - fw, vec2(0.20, 0.30) + fw, f)*smoothstep(vec2(0.80, 0.78) + fw, vec2(0.80, 0.78) - fw, f);
            float win = w0.x*w0.y;
            float slab = 1.0 - smoothstep(0.05, 0.05 + fw.y*2.0, f.y);
            vec3 wall = base*(0.90 + 0.16*noise(p*0.25))*(1.0 - 0.20*slab);
            vec3 pane = mix(vec3(0.030, 0.045, 0.065), envColor(reflect(-V, N), 0.0)*0.55, 0.40);
            pane += vec3(0.50, 0.40, 0.24)*0.10*step(0.86, hash(id));
            c = mix(wall*(amb + sunColor*ndl), pane, win);
        }
    }
    else
    {
        c = pow(colDiffuse.rgb, vec3(2.2))*(amb + sunColor*ndl);
    }
    c = applyFog(c);
    finalColor = vec4(tonemap(c), 1.0);
}
)";

static const char* VS_DEPTH = R"(#version 330
in vec3 vertexPosition;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;
uniform mat4 mvp;
uniform int skinned;
uniform mat4 boneMatrices[100];
void main()
{
    vec4 p = vec4(vertexPosition, 1.0);
    if (skinned == 1)
    {
        mat4 sk = vertexBoneWeights.x*boneMatrices[int(vertexBoneIndices.x)]
                + vertexBoneWeights.y*boneMatrices[int(vertexBoneIndices.y)]
                + vertexBoneWeights.z*boneMatrices[int(vertexBoneIndices.z)]
                + vertexBoneWeights.w*boneMatrices[int(vertexBoneIndices.w)];
        p = sk*p;
    }
    gl_Position = mvp*p;
}
)";
static const char* FS_DEPTH = R"(#version 330
void main() { }
)";

static const char* VS_SKY = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
void main() { gl_Position = mvp*vec4(vertexPosition, 1.0); }
)";

static const char* FS_SKY = R"(#version 330
uniform vec3 camF;
uniform vec3 camR;
uniform vec3 camU;
uniform float tanHalf;
uniform float aspect;
uniform vec2 resolution;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 horizonColor;
uniform vec3 zenithColor;
uniform float time;
uniform float exposure;
out vec4 finalColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p)
{
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; i++) { s += a*noise(p); p = p*2.02 + 11.0; a *= 0.5; }
    return s;
}
void main()
{
    vec2 ndc = gl_FragCoord.xy/resolution*2.0 - 1.0;
    vec3 d = normalize(camF + camR*ndc.x*aspect*tanHalf + camU*ndc.y*tanHalf);
    float h = clamp(d.y, 0.0, 1.0);
    vec3 c = mix(horizonColor, zenithColor, pow(h, 0.42));
    float sd = max(dot(d, sunDir), 0.0);
    c += sunColor*(pow(sd, 900.0)*8.0 + pow(sd, 60.0)*0.35 + pow(sd, 6.0)*0.12);
    if (d.y > 0.0)
    {
        vec2 uv = d.xz/(d.y + 0.12)*0.9 + vec2(time*0.006, time*0.002);
        float n = fbm(uv) + 0.35*fbm(uv*3.1 + 5.0);
        float cl = smoothstep(0.62, 1.02, n)*smoothstep(0.0, 0.16, d.y);
        vec3 cloud = mix(vec3(0.80, 0.84, 0.92), vec3(1.25, 1.18, 1.08), smoothstep(0.7, 1.15, n) + 0.35*pow(sd, 3.0));
        c = mix(c, cloud, cl*0.85);
    }
    // distant city and trees along the horizon
    float az = atan(d.x, -d.z);
    float blk = floor(az*120.0);
    float city = 0.006 + 0.020*hash(vec2(blk, 3.0))*step(0.45, hash(vec2(blk, 9.0))) + 0.005*hash(vec2(floor(az*320.0), 5.0));
    c = mix(c, mix(horizonColor, vec3(0.55, 0.60, 0.68), 0.55), smoothstep(city + 0.0015, city - 0.0005, d.y)*0.9);
    float tr = 0.004 + 0.011*noise(vec2(az*70.0, 1.0)) + 0.005*noise(vec2(az*230.0, 7.0));
    c = mix(c, mix(horizonColor, vec3(0.20, 0.33, 0.17), 0.70), smoothstep(tr + 0.0015, tr - 0.0005, d.y));
    c = mix(c, horizonColor, smoothstep(0.004, -0.004, d.y));
    c *= exposure;
    c = (c*(2.51*c + 0.03))/(c*(2.43*c + 0.59) + 0.14);
    finalColor = vec4(pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2)), 1.0);
}
)";


// Electronic backdrop: circuit traces on the back wall + a neon grid floor that
// is computed in world space, so 3D characters stand on it correctly.
static const char* FS_TECH = R"(#version 330
uniform vec3 camPos;
uniform vec3 camF;
uniform vec3 camR;
uniform vec3 camU;
uniform float tanHalf;
uniform float aspect;
uniform vec2 resolution;
uniform float time;
uniform vec3 tintL;
uniform vec3 tintR;
uniform vec4 padA;      // xyz = centre of a glowing platform, w = radius (0 = off)
uniform vec4 padB;
out vec4 finalColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
float hash1(float p) { return fract(sin(p*91.345)*47453.5453); }

// one layer of circuit traces. returns (trace, pulse)
vec2 circuit(vec2 p, float scale, float seed)
{
    vec2 q = p*scale;
    vec2 id = floor(q);
    vec2 f = fract(q) - 0.5;
    float r = hash(id + seed);
    float r2 = hash(id + seed + 17.0);
    float w = 0.035;
    float trace = 0.0, along = 0.0, len = 1.0;
    if (r < 0.34) { trace = smoothstep(w, 0.0, abs(f.y)); along = f.x + 0.5; }
    else if (r < 0.62) { trace = smoothstep(w, 0.0, abs(f.x)); along = f.y + 0.5; }
    else if (r < 0.80)
    {
        // corner piece
        float a = smoothstep(w, 0.0, abs(f.y))*step(f.x, 0.0);
        float b = smoothstep(w, 0.0, abs(f.x))*step(f.y, 0.0);
        trace = max(a, b);
        along = f.x < -abs(f.y) ? (f.x + 0.5) : (0.5 - f.y);
    }
    else if (r < 0.88)
    {
        // chip pad / node
        float d = length(f);
        trace = smoothstep(0.03, 0.0, abs(d - 0.16)) + smoothstep(0.07, 0.03, d);
        along = 0.5;
    }
    float speed = 0.25 + r2*0.6;
    float ph = fract(time*speed + r2*7.0);
    float pulse = trace*exp(-abs(along - ph)*9.0)*step(0.45, hash(id + seed + floor(time*speed + r2*7.0)));
    return vec2(trace, pulse);
}

void main()
{
    vec2 ndc = gl_FragCoord.xy/resolution*2.0 - 1.0;
    vec3 d = normalize(camF + camR*ndc.x*aspect*tanHalf + camU*ndc.y*tanHalf);
    vec2 sp = vec2(ndc.x*aspect, ndc.y);
    float side = smoothstep(-0.9, 0.9, sp.x);
    vec3 tint = mix(tintL, tintR, side);
    vec3 col = vec3(0.0);
    float horizonGlow = exp(-abs(d.y)*26.0);

    if (d.y < -0.0005)
    {
        // ---------------- floor ----------------
        float t = camPos.y/(-d.y);
        vec3 hit = camPos + d*t;
        vec2 w = hit.xz;
        float fade = exp(-t*0.055);
        col = vec3(0.006, 0.009, 0.020);
        // grid lines (1 m) with thicker lines every 4 m
        vec2 g = abs(fract(w) - 0.5);
        vec2 fw = fwidth(w);
        float l1 = max(smoothstep(0.5 - fw.x*1.5 - 0.012, 0.5, g.x), smoothstep(0.5 - fw.y*1.5 - 0.012, 0.5, g.y));
        vec2 g4 = abs(fract(w/4.0) - 0.5);
        float l4 = max(smoothstep(0.5 - fw.x*0.6 - 0.008, 0.5, g4.x), smoothstep(0.5 - fw.y*0.6 - 0.008, 0.5, g4.y));
        float detail = clamp(1.0 - max(fw.x, fw.y)*2.2, 0.0, 1.0);
        col += tint*(l1*0.20*detail + l4*0.55)*fade;
        // pulses racing along the lines towards the camera
        float lane = floor(w.x + 0.5);
        float pz = fract(w.y*0.07 + time*(0.22 + 0.3*hash1(lane)) + hash1(lane + 3.0));
        col += tint*smoothstep(0.5 - fw.x*1.5 - 0.02, 0.5, g.x)*exp(-pz*14.0)*1.6*fade*step(0.55, hash1(lane + 9.0));
        float lane2 = floor(w.y + 0.5);
        float px = fract(w.x*0.06*(hash1(lane2) > 0.5 ? 1.0 : -1.0) + time*(0.18 + 0.25*hash1(lane2 + 5.0)));
        col += tint*smoothstep(0.5 - fw.y*1.5 - 0.02, 0.5, g.y)*exp(-px*14.0)*1.3*fade*step(0.7, hash1(lane2 + 11.0));
        // glowing platforms under the fighters
        for (int i = 0; i < 2; i++)
        {
            vec4 pad = i == 0 ? padA : padB;
            if (pad.w <= 0.0) continue;
            vec3 pc = i == 0 ? tintL : tintR;
            float r = length(w - pad.xz);
            float aw = fwidth(r)*1.5;
            float ring = smoothstep(0.035 + aw, 0.0, abs(r - pad.w)) + 0.6*smoothstep(0.02 + aw, 0.0, abs(r - pad.w*0.78));
            float ang = atan(w.y - pad.z, w.x - pad.x);
            float dash = step(0.5, fract(ang*6.0/6.2832 + time*0.25*(i == 0 ? 1.0 : -1.0)));
            float ring2 = smoothstep(0.03 + aw, 0.0, abs(r - pad.w*1.16))*dash;
            float disc = smoothstep(pad.w, pad.w*0.2, r);
            col *= 1.0 - 0.75*smoothstep(pad.w*0.75, 0.0, r);                 // contact shadow
            col += pc*(ring*1.6 + ring2*0.9 + disc*0.10 + exp(-r*1.3)*0.10);
        }
        col += tint*horizonGlow*0.55;
        col = mix(vec3(0.010, 0.014, 0.032), col, clamp(fade*1.4, 0.0, 1.0));
    }
    else
    {
        // ---------------- back wall ----------------
        col = mix(vec3(0.012, 0.016, 0.038), vec3(0.004, 0.005, 0.014), clamp(d.y*2.2, 0.0, 1.0));
        vec2 p = sp + vec2(0.0, 0.0);
        vec2 c1 = circuit(p + vec2(0.0, time*0.004), 7.0, 1.0);
        vec2 c2 = circuit(p*1.0 + 3.7, 15.0, 41.0);
        float wallFade = smoothstep(1.25, 0.05, ndc.y)*(0.55 + 0.45*abs(sp.x)/aspect);
        col += tint*(c1.x*0.085 + c1.y*1.25 + c2.x*0.035 + c2.y*0.55)*wallFade;
        // HUD rings behind the centre
        vec2 rc = sp - vec2(0.0, 0.16);
        float rr = length(rc);
        float ang = atan(rc.y, rc.x);
        float rings = 0.0;
        rings += smoothstep(0.006, 0.0, abs(rr - 0.52))*step(0.35, fract(ang*4.0/6.2832 + time*0.03));
        rings += smoothstep(0.004, 0.0, abs(rr - 0.60))*step(0.6, fract(ang*12.0/6.2832 - time*0.05));
        rings += smoothstep(0.010, 0.0, abs(rr - 0.44))*step(0.8, fract(ang*36.0/6.2832 + time*0.02))*0.7;
        rings += smoothstep(0.003, 0.0, abs(rr - 0.70))*0.5;
        col += mix(tintL, tintR, 0.5)*rings*0.38*smoothstep(-0.1, 0.2, ndc.y + 0.3);
        // slow vertical scan beam and data columns
        float scan = exp(-abs(ndc.y - (fract(time*0.07)*2.6 - 1.0))*40.0);
        col += tint*scan*0.05;
        float colId = floor(sp.x*26.0);
        float stream = step(0.82, hash1(colId))*smoothstep(0.08, 0.0, abs(fract(sp.x*26.0) - 0.5) - 0.1);
        float sv = fract(ndc.y*0.35 + time*(0.05 + 0.12*hash1(colId + 2.0)) + hash1(colId + 6.0));
        col += tint*stream*exp(-sv*7.0)*0.20*step(0.5, fract(ndc.y*38.0 + time*2.0*hash1(colId)));
        col += tint*horizonGlow*0.9 + vec3(1.0)*exp(-abs(d.y)*220.0)*0.35;
    }
    // soft coloured light from both sides
    col += tintL*0.10*exp(-length(sp - vec2(-aspect*0.85, -0.1))*1.4);
    col += tintR*0.10*exp(-length(sp - vec2(aspect*0.85, -0.1))*1.4);
    // scan lines, vignette, film grain
    col *= 0.93 + 0.07*sin(gl_FragCoord.y*2.2);
    col *= 1.0 - 0.35*dot(ndc*0.75, ndc*0.75);
    col += (hash(gl_FragCoord.xy + time) - 0.5)*0.012;
    col = col/(1.0 + col*0.35);
    finalColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0/2.2)), 1.0);
}
)";

// ---------------------------------------------------------------------------
void Renderer::fetchLocs(Shader& sh, LitLocs& l)
{
    l.viewPos = GetShaderLocation(sh, "viewPos");
    l.sunDir = GetShaderLocation(sh, "sunDir");
    l.sunColor = GetShaderLocation(sh, "sunColor");
    l.skyColor = GetShaderLocation(sh, "skyColor");
    l.groundColor = GetShaderLocation(sh, "groundColor");
    l.fogColor = GetShaderLocation(sh, "fogColor");
    l.fogDensity = GetShaderLocation(sh, "fogDensity");
    l.exposure = GetShaderLocation(sh, "exposure");
    l.lightVP = GetShaderLocation(sh, "lightVP");
    l.shadowMap = GetShaderLocation(sh, "shadowMap");
    l.skinned = GetShaderLocation(sh, "skinned");
    l.bones = GetShaderLocation(sh, "boneMatrices");
    l.flash = GetShaderLocation(sh, "flashColor");
    l.rim = GetShaderLocation(sh, "rimColor");
    l.lit = GetShaderLocation(sh, "litAmount");
    l.useNormal = GetShaderLocation(sh, "useNormalMap");
    l.useOrm = GetShaderLocation(sh, "useORM");
    l.saturation = GetShaderLocation(sh, "saturation");
    l.shadowOn = GetShaderLocation(sh, "shadowOn");
    sh.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(sh, "texORM");
}

bool Renderer::init()
{
    std::string fsLit = std::string(FS_COMMON) + FS_LIT_MAIN;
    std::string fsGround = std::string(FS_COMMON) + FS_GROUND_MAIN;
    lit = LoadShaderFromMemory(VS_LIT, fsLit.c_str());
    ground = LoadShaderFromMemory(VS_LIT, fsGround.c_str());
    depth = LoadShaderFromMemory(VS_DEPTH, FS_DEPTH);
    sky = LoadShaderFromMemory(VS_SKY, FS_SKY);
    std::string fsBloc = std::string(FS_COMMON) + FS_BLOC_MAIN;
    bloc = LoadShaderFromMemory(VS_LIT, fsBloc.c_str());
    tech = LoadShaderFromMemory(VS_SKY, FS_TECH);
    if (!IsShaderValid(lit) || !IsShaderValid(ground) || !IsShaderValid(depth) || !IsShaderValid(sky) || !IsShaderValid(tech) || !IsShaderValid(bloc)) return false;
    T.camPos = GetShaderLocation(tech, "camPos"); T.camF = GetShaderLocation(tech, "camF"); T.camR = GetShaderLocation(tech, "camR");
    T.camU = GetShaderLocation(tech, "camU"); T.tanHalf = GetShaderLocation(tech, "tanHalf"); T.aspect = GetShaderLocation(tech, "aspect");
    T.res = GetShaderLocation(tech, "resolution"); T.time = GetShaderLocation(tech, "time");
    T.tintL = GetShaderLocation(tech, "tintL"); T.tintR = GetShaderLocation(tech, "tintR");
    T.padA = GetShaderLocation(tech, "padA"); T.padB = GetShaderLocation(tech, "padB");

    fetchLocs(lit, L);
    fetchLocs(ground, G);
    fetchLocs(bloc, B);
    bMat = GetShaderLocation(bloc, "matId"); bSign = GetShaderLocation(bloc, "signRect");
    bZenith = GetShaderLocation(bloc, "zenithColor"); bGlassTop = GetShaderLocation(bloc, "glassTop");
    bTime = GetShaderLocation(bloc, "time");
    matBloc = LoadMaterialDefault(); matBloc.shader = bloc;
    gArena = GetShaderLocation(ground, "arena");
    gTime = GetShaderLocation(ground, "time");
    dSkinned = GetShaderLocation(depth, "skinned");
    dBones = GetShaderLocation(depth, "boneMatrices");
    S.camF = GetShaderLocation(sky, "camF"); S.camR = GetShaderLocation(sky, "camR"); S.camU = GetShaderLocation(sky, "camU");
    S.tanHalf = GetShaderLocation(sky, "tanHalf"); S.aspect = GetShaderLocation(sky, "aspect");
    S.res = GetShaderLocation(sky, "resolution"); S.sunDir = GetShaderLocation(sky, "sunDir");
    S.sunColor = GetShaderLocation(sky, "sunColor"); S.horizon = GetShaderLocation(sky, "horizonColor");
    S.zenith = GetShaderLocation(sky, "zenithColor"); S.time = GetShaderLocation(sky, "time");
    S.exposure = GetShaderLocation(sky, "exposure");

    matLit = LoadMaterialDefault();   matLit.shader = lit;
    matDepth = LoadMaterialDefault(); matDepth.shader = depth;
    matGround = LoadMaterialDefault(); matGround.shader = ground;
    groundMesh = GenMeshPlane(900.0f, 900.0f, 1, 1);

    Image wi = GenImageColor(2, 2, WHITE);
    white = LoadTextureFromImage(wi);
    UnloadImage(wi);

    // depth-only framebuffer for the shadow map
    shadowMap.id = rlLoadFramebuffer();
    shadowMap.texture.width = SHADOW_SIZE;
    shadowMap.texture.height = SHADOW_SIZE;
    if (shadowMap.id > 0)
    {
        rlEnableFramebuffer(shadowMap.id);
        shadowMap.depth.id = rlLoadTextureDepth(SHADOW_SIZE, SHADOW_SIZE, false);
        shadowMap.depth.width = SHADOW_SIZE;
        shadowMap.depth.height = SHADOW_SIZE;
        shadowMap.depth.format = 19;
        shadowMap.depth.mipmaps = 1;
        rlFramebufferAttach(shadowMap.id, shadowMap.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferComplete(shadowMap.id);
        rlDisableFramebuffer();
    }
    return true;
}

void Renderer::shutdown()
{
    UnloadShader(lit); UnloadShader(ground); UnloadShader(depth); UnloadShader(sky); UnloadShader(tech); UnloadShader(bloc);
    UnloadMesh(groundMesh);
    UnloadTexture(white);
    if (shadowMap.id > 0) { rlUnloadTexture(shadowMap.depth.id); rlUnloadFramebuffer(shadowMap.id); }
}

// ---------------------------------------------------------------------------
// Shadow pass
// ---------------------------------------------------------------------------
void Renderer::beginShadows(Vector3 focus, float radius)
{
    rlDrawRenderBatchActive();
    Camera3D lc{};
    lc.position = focus + Vector3Scale(env.sunDir, 30.0f);
    lc.target = focus;
    lc.up = v3(0, 1, 0);
    lc.fovy = radius * 2.0f;
    lc.projection = CAMERA_ORTHOGRAPHIC;
    rlSetClipPlanes(1.0, 70.0);
    BeginTextureMode(shadowMap);
    ClearBackground(WHITE);
    BeginMode3D(lc);
    lightVP = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    rlDisableBackfaceCulling();
    shadowsValid = true;
}

void Renderer::shadowSkinned(const GpuModel& m, const std::vector<Matrix>& bones, Matrix world)
{
    int one = 1;
    SetShaderValue(depth, dSkinned, &one, SHADER_UNIFORM_INT);
    rlSetUniformMatrices(dBones, bones.data(), std::min((int)bones.size(), MAX_BONES));
    for (const Mesh& mesh : m.meshes) DrawMesh(mesh, matDepth, world);
}

void Renderer::shadowStatic(const GpuModel& m, Matrix world)
{
    int zero = 0;
    SetShaderValue(depth, dSkinned, &zero, SHADER_UNIFORM_INT);
    for (const Mesh& mesh : m.meshes) DrawMesh(mesh, matDepth, world);
}

void Renderer::endShadows()
{
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();
    rlSetClipPlanes(0.15, 900.0);
}

void Renderer::clearShadows() { shadowsValid = false; }

// ---------------------------------------------------------------------------
// Main pass
// ---------------------------------------------------------------------------
void Renderer::applyCommon(Shader& sh, LitLocs& l)
{
    SetShaderValue(sh, l.viewPos, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.sunDir, &env.sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.sunColor, &env.sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.skyColor, &env.skyColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.groundColor, &env.groundColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.fogColor, &env.horizonColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.fogDensity, &env.fogDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.exposure, &env.exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValueMatrix(sh, l.lightVP, lightVP);
    float on = (shadowsValid && shadowsEnabled) ? 1.0f : 0.0f;
    SetShaderValue(sh, l.shadowOn, &on, SHADER_UNIFORM_FLOAT);
    int slot = SHADOW_SLOT;
    rlEnableShader(sh.id);
    rlActiveTextureSlot(SHADOW_SLOT);
    rlEnableTexture(shadowMap.depth.id);
    rlSetUniform(l.shadowMap, &slot, SHADER_UNIFORM_INT, 1);
    rlActiveTextureSlot(0);
}

void Renderer::beginScene(Camera3D cam, bool drawSky)
{
    camera = cam;
    rlDrawRenderBatchActive();
    if (drawSky)
    {
        Vector3 f = Vector3Normalize(cam.target - cam.position);
        Vector3 r = Vector3Normalize(Vector3CrossProduct(f, cam.up));
        Vector3 u = Vector3CrossProduct(r, f);
        float tanHalf = tanf(cam.fovy * 0.5f * DEG2RAD);
        float aspect = (float)GetRenderWidth() / (float)GetRenderHeight();
        Vector2 res = { (float)GetRenderWidth(), (float)GetRenderHeight() };
        SetShaderValue(sky, S.camF, &f, SHADER_UNIFORM_VEC3);
        SetShaderValue(sky, S.camR, &r, SHADER_UNIFORM_VEC3);
        SetShaderValue(sky, S.camU, &u, SHADER_UNIFORM_VEC3);
        SetShaderValue(sky, S.tanHalf, &tanHalf, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sky, S.aspect, &aspect, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sky, S.res, &res, SHADER_UNIFORM_VEC2);
        SetShaderValue(sky, S.sunDir, &env.sunDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(sky, S.sunColor, &env.sunColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(sky, S.horizon, &env.horizonColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(sky, S.zenith, &env.zenithColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(sky, S.time, &env.time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sky, S.exposure, &env.exposure, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(sky);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), WHITE);
        EndShaderMode();
    }
    BeginMode3D(cam);
    applyCommon(lit, L);
    applyCommon(ground, G);
    applyCommon(bloc, B);
    SetShaderValue(bloc, bZenith, &env.zenithColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(bloc, bTime, &env.time, SHADER_UNIFORM_FLOAT);
}

void Renderer::drawSkinned(const GpuModel& m, const std::vector<Matrix>& bones, Matrix world, const SurfaceParams& sp)
{
    if (!m.ready) return;
    int one = 1;
    float useN = m.normal.id ? 1.0f : 0.0f, useO = m.orm.id ? 1.0f : 0.0f;
    SetShaderValue(lit, L.skinned, &one, SHADER_UNIFORM_INT);
    rlSetUniformMatrices(L.bones, bones.data(), std::min((int)bones.size(), MAX_BONES));
    SetShaderValue(lit, L.flash, &sp.flash, SHADER_UNIFORM_VEC4);
    SetShaderValue(lit, L.rim, &sp.rim, SHADER_UNIFORM_VEC4);
    SetShaderValue(lit, L.lit, &sp.lit, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, L.useNormal, &useN, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, L.useOrm, &useO, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, L.saturation, &sp.saturation, SHADER_UNIFORM_FLOAT);
    matLit.maps[MATERIAL_MAP_ALBEDO].texture = m.albedo.id ? m.albedo : white;
    matLit.maps[MATERIAL_MAP_ALBEDO].color = sp.tint;
    matLit.maps[MATERIAL_MAP_NORMAL].texture = m.normal;
    matLit.maps[MATERIAL_MAP_ROUGHNESS].texture = m.orm;
    rlDisableBackfaceCulling();       // character meshes are double sided
    for (const Mesh& mesh : m.meshes) DrawMesh(mesh, matLit, world);
    rlEnableBackfaceCulling();
}

void Renderer::drawStatic(const GpuModel& m, Matrix world, const SurfaceParams& sp)
{
    if (!m.ready) return;
    int zero = 0;
    float useN = (m.normal.id && sp.lit > 0.01f) ? 1.0f : 0.0f, useO = m.orm.id ? 1.0f : 0.0f;
    SetShaderValue(lit, L.skinned, &zero, SHADER_UNIFORM_INT);
    SetShaderValue(lit, L.flash, &sp.flash, SHADER_UNIFORM_VEC4);
    SetShaderValue(lit, L.rim, &sp.rim, SHADER_UNIFORM_VEC4);
    SetShaderValue(lit, L.lit, &sp.lit, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, L.useNormal, &useN, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, L.useOrm, &useO, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit, L.saturation, &sp.saturation, SHADER_UNIFORM_FLOAT);
    matLit.maps[MATERIAL_MAP_ALBEDO].texture = m.albedo.id ? m.albedo : white;
    matLit.maps[MATERIAL_MAP_ALBEDO].color = sp.tint;
    matLit.maps[MATERIAL_MAP_NORMAL].texture = m.normal;
    matLit.maps[MATERIAL_MAP_ROUGHNESS].texture = m.orm;
    if (sp.doubleSided) rlDisableBackfaceCulling();
    for (const Mesh& mesh : m.meshes) DrawMesh(mesh, matLit, world);
    if (sp.doubleSided) rlEnableBackfaceCulling();
}

void Renderer::drawGround(float arenaHalfWidth, bool campus)
{
    int zero = 0;
    Vector4 arena = { arenaHalfWidth, 2.6f, 0.0f, campus ? 1.0f : 0.0f };
    SetShaderValue(ground, G.skinned, &zero, SHADER_UNIFORM_INT);
    SetShaderValue(ground, gArena, &arena, SHADER_UNIFORM_VEC4);
    SetShaderValue(ground, gTime, &env.time, SHADER_UNIFORM_FLOAT);
    matGround.maps[MATERIAL_MAP_ALBEDO].texture = white;
    DrawMesh(groundMesh, matGround, MatrixIdentity());
}

void Renderer::endScene()
{
    EndMode3D();
}

void Renderer::setExposure(float e)
{
    rlDrawRenderBatchActive();
    SetShaderValue(lit, L.exposure, &e, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ground, G.exposure, &e, SHADER_UNIFORM_FLOAT);
    SetShaderValue(bloc, B.exposure, &e, SHADER_UNIFORM_FLOAT);
}

void Renderer::drawTechBackdrop(Camera3D cam, Vector3 tintLeft, Vector3 tintRight, Vector4 padA, Vector4 padB, float time)
{
    rlDrawRenderBatchActive();
    Vector3 f = Vector3Normalize(cam.target - cam.position);
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, cam.up));
    Vector3 u = Vector3CrossProduct(r, f);
    float tanHalf = tanf(cam.fovy * 0.5f * DEG2RAD);
    float aspect = (float)GetRenderWidth() / (float)GetRenderHeight();
    Vector2 res = { (float)GetRenderWidth(), (float)GetRenderHeight() };
    SetShaderValue(tech, T.camPos, &cam.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(tech, T.camF, &f, SHADER_UNIFORM_VEC3);
    SetShaderValue(tech, T.camR, &r, SHADER_UNIFORM_VEC3);
    SetShaderValue(tech, T.camU, &u, SHADER_UNIFORM_VEC3);
    SetShaderValue(tech, T.tanHalf, &tanHalf, SHADER_UNIFORM_FLOAT);
    SetShaderValue(tech, T.aspect, &aspect, SHADER_UNIFORM_FLOAT);
    SetShaderValue(tech, T.res, &res, SHADER_UNIFORM_VEC2);
    SetShaderValue(tech, T.time, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(tech, T.tintL, &tintLeft, SHADER_UNIFORM_VEC3);
    SetShaderValue(tech, T.tintR, &tintRight, SHADER_UNIFORM_VEC3);
    SetShaderValue(tech, T.padA, &padA, SHADER_UNIFORM_VEC4);
    SetShaderValue(tech, T.padB, &padB, SHADER_UNIFORM_VEC4);
    BeginShaderMode(tech);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), WHITE);
    EndShaderMode();
}

void Renderer::drawProc(const Mesh& mesh, int mat, Color tint, Texture2D sign, Vector4 signRect, float glassTop, bool doubleSided)
{
    int zero = 0;
    SetShaderValue(bloc, B.skinned, &zero, SHADER_UNIFORM_INT);
    SetShaderValue(bloc, bMat, &mat, SHADER_UNIFORM_INT);
    SetShaderValue(bloc, bSign, &signRect, SHADER_UNIFORM_VEC4);
    SetShaderValue(bloc, bGlassTop, &glassTop, SHADER_UNIFORM_FLOAT);
    matBloc.maps[MATERIAL_MAP_ALBEDO].texture = sign.id ? sign : white;
    matBloc.maps[MATERIAL_MAP_ALBEDO].color = tint;
    if (doubleSided) rlDisableBackfaceCulling();
    DrawMesh(mesh, matBloc, MatrixIdentity());
    if (doubleSided) rlEnableBackfaceCulling();
}
