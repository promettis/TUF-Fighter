// ============================================================================
//  TUF FIGHTER - stage_bloc.cpp
//  "The University Bloc" stage, built in code after photos of the real
//  building: a white panel-clad block with gold lettering, a slanted black
//  glass entrance, dark steps, potted palms, flag poles and trees.
//  The surfaces are shaded procedurally (see FS_BLOC_MAIN in render.cpp).
// ============================================================================
#include "game.h"

namespace
{
    enum { MAT_PANEL = 0, MAT_GLASS = 1, MAT_GRANITE = 2, MAT_PLAIN = 3, MAT_LEAF = 5, MAT_BRICK = 6, MAT_FACADE = 7 };

    // Collects triangles and turns them into a GPU mesh
    struct MeshBuilder
    {
        std::vector<float> pos, nrm, uv;
        std::vector<unsigned short> idx;

        int vert(Vector3 p, Vector3 n)
        {
            pos.insert(pos.end(), { p.x, p.y, p.z });
            nrm.insert(nrm.end(), { n.x, n.y, n.z });
            uv.insert(uv.end(), { 0.0f, 0.0f });
            return (int)(pos.size() / 3) - 1;
        }
        Vector3 P(int i) const { return v3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]); }
        Vector3 N(int i) const { return v3(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]); }

        // triangle between existing vertices; the winding is fixed so it agrees with the vertex normals
        void tri(int a, int b, int c)
        {
            Vector3 g = Vector3CrossProduct(P(b) - P(a), P(c) - P(a));
            if (Vector3DotProduct(g, N(a) + N(b) + N(c)) < 0.0f) std::swap(b, c);
            idx.push_back((unsigned short)a); idx.push_back((unsigned short)b); idx.push_back((unsigned short)c);
        }
        // flat quad; `facing` is any vector pointing to the visible side
        void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 facing)
        {
            Vector3 n = Vector3Normalize(Vector3CrossProduct(b - a, d - a));
            if (Vector3DotProduct(n, facing) < 0.0f) n = Vector3Negate(n);
            int i0 = vert(a, n), i1 = vert(b, n), i2 = vert(c, n), i3 = vert(d, n);
            tri(i0, i1, i2); tri(i0, i2, i3);
        }
        void box(Vector3 mn, Vector3 mx)
        {
            Vector3 a = mn, b = mx;
            quad(v3(a.x, a.y, b.z), v3(b.x, a.y, b.z), v3(b.x, b.y, b.z), v3(a.x, b.y, b.z), v3(0, 0, 1));
            quad(v3(a.x, a.y, a.z), v3(b.x, a.y, a.z), v3(b.x, b.y, a.z), v3(a.x, b.y, a.z), v3(0, 0, -1));
            quad(v3(a.x, a.y, a.z), v3(a.x, a.y, b.z), v3(a.x, b.y, b.z), v3(a.x, b.y, a.z), v3(-1, 0, 0));
            quad(v3(b.x, a.y, a.z), v3(b.x, a.y, b.z), v3(b.x, b.y, b.z), v3(b.x, b.y, a.z), v3(1, 0, 0));
            quad(v3(a.x, b.y, a.z), v3(b.x, b.y, a.z), v3(b.x, b.y, b.z), v3(a.x, b.y, b.z), v3(0, 1, 0));
        }
        // cone frustum standing on `base`
        void frustum(Vector3 base, float r0, float r1, float h, int seg, bool cap)
        {
            float slope = (r0 - r1) / std::max(h, 0.001f);
            int first = (int)(pos.size() / 3);
            for (int i = 0; i <= seg; i++)
            {
                float a = (float)i / seg * 2.0f * PI;
                Vector3 d = v3(cosf(a), 0, sinf(a));
                Vector3 n = Vector3Normalize(v3(d.x, slope, d.z));
                vert(base + Vector3Scale(d, r0), n);
                vert(base + Vector3Scale(d, r1) + v3(0, h, 0), n);
            }
            for (int i = 0; i < seg; i++)
            {
                int k = first + i * 2;
                tri(k, k + 1, k + 3); tri(k, k + 3, k + 2);
            }
            if (cap)
            {
                int c = vert(base + v3(0, h, 0), v3(0, 1, 0));
                int ring = (int)(pos.size() / 3);
                for (int i = 0; i <= seg; i++)
                {
                    float a = (float)i / seg * 2.0f * PI;
                    vert(base + v3(cosf(a) * r1, h, sinf(a) * r1), v3(0, 1, 0));
                }
                for (int i = 0; i < seg; i++) tri(c, ring + i, ring + i + 1);
            }
        }
        // lumpy ellipsoid (tree crowns)
        void blob(Vector3 c, Vector3 r, int seg, int rings, float lump, Rng& rng)
        {
            int first = (int)(pos.size() / 3);
            float ph = rng.range(0.0f, 6.0f);
            for (int j = 0; j <= rings; j++)
            {
                float v = (float)j / rings * PI;
                for (int i = 0; i <= seg; i++)
                {
                    float u = (float)i / seg * 2.0f * PI;
                    Vector3 d = v3(sinf(v) * cosf(u), cosf(v), sinf(v) * sinf(u));
                    float k = 1.0f + lump * (sinf(u * 3.0f + ph) * sinf(v * 4.0f + ph * 1.7f) + 0.5f * sinf(u * 7.0f + v * 5.0f + ph));
                    vert(c + v3(d.x * r.x * k, d.y * r.y * k, d.z * r.z * k), d);
                }
            }
            for (int j = 0; j < rings; j++)
                for (int i = 0; i < seg; i++)
                {
                    int a = first + j * (seg + 1) + i, b = a + seg + 1;
                    if (j > 0) tri(a, b, a + 1);
                    if (j < rings - 1) tri(a + 1, b, b + 1);
                }
        }
        // one arching palm frond
        void frond(Vector3 root, float yaw, float length, float rise, float width)
        {
            const int SEG = 7;
            Vector3 dir = v3(cosf(yaw), 0, sinf(yaw)), side = v3(-sinf(yaw), 0, cosf(yaw));
            int first = (int)(pos.size() / 3);
            for (int i = 0; i <= SEG; i++)
            {
                float t = (float)i / SEG;
                float h = rise * sinf(t * PI * 0.62f) - rise * 0.95f * t * t * t;
                Vector3 c = root + Vector3Scale(dir, length * t) + v3(0, h, 0);
                float w = width * powf(sinf(t * PI * 0.94f + 0.06f), 0.75f) * (1.0f - 0.35f * t);
                float dh = rise * (cosf(t * PI * 0.62f) * 0.62f * PI - 2.85f * t * t) / length;
                Vector3 n = Vector3Normalize(v3(-dir.x * dh, 1.0f, -dir.z * dh));
                // the leaflets droop a little on both sides of the rib
                vert(c + Vector3Scale(side, w) + v3(0, -w * 0.35f, 0), Vector3Normalize(n + Vector3Scale(side, 0.45f)));
                vert(c, n);
                vert(c - Vector3Scale(side, w) + v3(0, -w * 0.35f, 0), Vector3Normalize(n - Vector3Scale(side, 0.45f)));
            }
            for (int i = 0; i < SEG; i++)
            {
                int k = first + i * 3;
                tri(k, k + 3, k + 4); tri(k, k + 4, k + 1);
                tri(k + 1, k + 4, k + 5); tri(k + 1, k + 5, k + 2);
            }
        }

        bool empty() const { return idx.empty(); }
        Mesh upload()
        {
            Mesh m{};
            m.vertexCount = (int)(pos.size() / 3);
            m.triangleCount = (int)(idx.size() / 3);
            auto dup = [](const void* src, size_t bytes) { void* p = MemAlloc((unsigned int)bytes); memcpy(p, src, bytes); return p; };
            m.vertices = (float*)dup(pos.data(), pos.size() * sizeof(float));
            m.normals = (float*)dup(nrm.data(), nrm.size() * sizeof(float));
            m.texcoords = (float*)dup(uv.data(), uv.size() * sizeof(float));
            m.indices = (unsigned short*)dup(idx.data(), idx.size() * sizeof(unsigned short));
            UploadMesh(&m, false);
            return m;
        }
    };
}

// ---------------------------------------------------------------------------
void StageDef::buildUniversityBloc(const std::string& fontFile)
{
    procedural = true;
    Rng rng; rng.s = 20261117u;

    // ---- dimensions (metres). The fighters stand at z = 0, the camera looks towards -Z ----
    // The building is symmetrical: the glass entrance sits in the middle of the facade.
    const float X0 = -24.0f, X1 = 24.0f;         // building left / right
    const float H = 15.2f;                       // roof height
    const float ZF = -58.0f, ZB = -92.0f;        // front facade / back wall
    // slanted entrance opening in the facade (bottom is wider than the top)
    const float BL = -9.2f, BR = 9.2f, TL = -5.6f, TR = 5.6f, HT = 8.2f;
    const float ZG = ZF - 1.7f;                  // the glass sits back inside the opening
    const float TERR = 0.9f;                     // height of the terrace in front
    glassTop = HT;

    MeshBuilder wall, glass, granite;
    // front facade around the opening
    wall.quad(v3(X0, 0, ZF), v3(BL, 0, ZF), v3(TL, HT, ZF), v3(X0, HT, ZF), v3(0, 0, 1));
    wall.quad(v3(BR, 0, ZF), v3(X1, 0, ZF), v3(X1, HT, ZF), v3(TR, HT, ZF), v3(0, 0, 1));
    wall.quad(v3(X0, HT, ZF), v3(X1, HT, ZF), v3(X1, H, ZF), v3(X0, H, ZF), v3(0, 0, 1));
    // the reveals of the opening
    wall.quad(v3(BL, 0, ZF), v3(BL, 0, ZG), v3(TL, HT, ZG), v3(TL, HT, ZF), v3(1, -0.4f, 0));
    wall.quad(v3(BR, 0, ZG), v3(BR, 0, ZF), v3(TR, HT, ZF), v3(TR, HT, ZG), v3(-1, -0.4f, 0));
    wall.quad(v3(TL, HT, ZF), v3(TL, HT, ZG), v3(TR, HT, ZG), v3(TR, HT, ZF), v3(0, -1, 0));
    // sides, roof, back
    wall.quad(v3(X0, 0, ZB), v3(X0, 0, ZF), v3(X0, H, ZF), v3(X0, H, ZB), v3(-1, 0, 0));
    wall.quad(v3(X1, 0, ZF), v3(X1, 0, ZB), v3(X1, H, ZB), v3(X1, H, ZF), v3(1, 0, 0));
    wall.quad(v3(X0, H, ZF), v3(X1, H, ZF), v3(X1, H, ZB), v3(X0, H, ZB), v3(0, 1, 0));
    wall.quad(v3(X0, 0, ZB), v3(X1, 0, ZB), v3(X1, H, ZB), v3(X0, H, ZB), v3(0, 0, -1));
    glass.quad(v3(BL, 0, ZG), v3(BR, 0, ZG), v3(TR, HT, ZG), v3(TL, HT, ZG), v3(0, 0, 1));

    // terrace and steps along the whole front
    const float TZ = ZF + 4.4f;
    granite.box(v3(X0 - 0.8f, 0, ZG - 0.2f), v3(X1 + 0.8f, TERR, TZ));
    for (int k = 0; k < 2; k++)
        granite.box(v3(X0 - 0.8f, 0, TZ + 0.5f * k), v3(X1 + 0.8f, TERR - 0.3f * (k + 1), TZ + 0.5f * (k + 1)));
    // door frames in front of the glass
    MeshBuilder metal;
    for (int i = -2; i <= 2; i++)
        metal.box(v3(i * 1.15f - 0.05f, TERR, ZG + 0.02f), v3(i * 1.15f + 0.05f, TERR + 2.5f, ZG + 0.12f));
    metal.box(v3(-2.35f, TERR + 2.45f, ZG + 0.02f), v3(2.35f, TERR + 2.6f, ZG + 0.12f));

    // ---- potted palms, mirrored left / right ----
    MeshBuilder pots, soil, leaves;
    auto palm = [&](float x, float y, float z, float s) {
        pots.frustum(v3(x, y, z), 0.36f * s, 0.55f * s, 0.85f * s, 14, false);
        pots.frustum(v3(x, y + 0.85f * s, z), 0.60f * s, 0.60f * s, 0.07f * s, 14, false);
        soil.frustum(v3(x, y + 0.60f * s, z), 0.50f * s, 0.14f * s, 0.75f * s, 10, true);
        Vector3 crown = v3(x, y + 1.30f * s, z);
        int n = 13;
        for (int i = 0; i < n; i++)
        {
            float yaw = (float)i / n * 2.0f * PI + rng.range(-0.2f, 0.2f);
            bool inner = (i % 3 == 0);
            leaves.frond(crown, yaw, (inner ? 1.15f : 1.85f) * s * rng.range(0.9f, 1.1f), (inner ? 1.25f : 0.75f) * s * rng.range(0.85f, 1.15f), 0.23f * s);
        }
    };
    const float PZ = TZ - 0.9f;
    for (float side = -1.0f; side <= 1.0f; side += 2.0f)
    {
        palm(side * 11.2f, TERR, PZ, 1.0f);
        palm(side * 15.6f, TERR, PZ, 1.0f);
        palm(side * 20.0f, TERR, PZ, 1.0f);
        palm(side * 23.2f, TERR, PZ, 1.0f);
        palm(side * 7.6f, TERR, ZG + 1.0f, 0.9f);
        palm(side * 13.4f, 0.0f, TZ + 2.2f, 1.15f);
        palm(side * 21.5f, 0.0f, TZ + 2.2f, 1.15f);
    }

    // ---- flag poles at the front left corner ----
    MeshBuilder flags[6];
    const Color FLAG_COL[6] = { { 60, 120, 210, 255 }, { 230, 190, 50, 255 }, { 205, 60, 55, 255 }, { 240, 240, 240, 255 }, { 50, 150, 90, 255 }, { 40, 70, 170, 255 } };
    for (int i = 0; i < 6; i++)
    {
        float x = -29.2f + i * 1.25f, z = ZF + 11.5f + i * 0.4f, h = 8.6f;
        metal.frustum(v3(x, 0, z), 0.07f, 0.045f, h, 8, true);
        float w = 1.55f, fh = 0.95f;
        for (int k = 0; k < 3; k++)
        {
            float a0 = k / 3.0f, a1 = (k + 1) / 3.0f;
            auto pt = [&](float a, float yy) { return v3(x + 0.05f + w * a, h - 0.15f - yy - 0.22f * a * a, z + 0.16f * sinf(a * 5.0f + i)); };
            flags[i].quad(pt(a0, 0), pt(a1, 0), pt(a1, fh), pt(a0, fh), v3(0, 0, 1));
        }
    }

    // ---- lamp posts along the drive ----
    MeshBuilder lamps;
    auto lamp = [&](float x, float z) {
        metal.frustum(v3(x, 0, z), 0.10f, 0.06f, 5.4f, 8, false);
        metal.box(v3(x - 0.05f, 5.3f, z - 0.05f), v3(x + 0.05f, 5.45f, z + 0.9f));
        lamps.box(v3(x - 0.22f, 5.18f, z + 0.5f), v3(x + 0.22f, 5.32f, z + 1.1f));
    };
    for (float x : { -34.0f, -17.5f, 17.5f, 34.0f }) lamp(x, -35.2f);

    // ---- trees on the lawns beside the building ----
    MeshBuilder trunks, crowns[3];
    int treeNo = 0;
    auto tree = [&](float x, float z, float s) {
        trunks.frustum(v3(x, 0, z), 0.30f * s, 0.17f * s, 3.4f * s, 8, false);
        MeshBuilder& cr = crowns[treeNo++ % 3];
        cr.blob(v3(x, 5.6f * s, z), v3(2.6f * s, 2.3f * s, 2.6f * s), 14, 9, 0.09f, rng);
        for (int k = 0; k < 4; k++)
        {
            float a = k * 1.57f + rng.range(0.0f, 1.0f), r = rng.range(1.2f, 1.9f) * s;
            cr.blob(v3(x + cosf(a) * r, (4.3f + rng.range(0.0f, 1.0f)) * s, z + sinf(a) * r),
                    v3(1.7f * s, 1.45f * s, 1.7f * s), 11, 7, 0.10f, rng);
        }
    };
    const float TREES[11][3] = {
        { 28.5f, -49.5f, 1.00f }, { 33.5f, -57.0f, 1.25f }, { 30.0f, -67.0f, 1.40f }, { 39.0f, -47.5f, 1.10f }, { 43.0f, -61.0f, 1.50f },
        { 49.0f, -52.0f, 1.20f }, { 37.0f, -79.0f, 1.50f }, { 53.0f, -71.0f, 1.60f }, { 59.0f, -58.0f, 1.30f }, { 47.0f, -88.0f, 1.60f }, { 64.0f, -84.0f, 1.50f } };
    for (int i = 0; i < 11; i++)
    {
        tree(TREES[i][0] + rng.range(-0.8f, 0.8f), TREES[i][1], TREES[i][2]);
        tree(-TREES[i][0] + rng.range(-0.8f, 0.8f), TREES[i][1] + rng.range(-1.5f, 1.5f), TREES[i][2] * rng.range(0.9f, 1.1f));
    }
    // a few more further out, seen when the camera swings round
    for (float side = -1.0f; side <= 1.0f; side += 2.0f)
        for (int i = 0; i < 5; i++) tree(side * (70.0f + i * 7.0f), -18.0f - i * 16.0f, rng.range(1.2f, 1.7f));

    // ---- hedges and shrubs ----
    MeshBuilder hedge, shrubs;
    for (float side = -1.0f; side <= 1.0f; side += 2.0f)
    {
        float a = std::min(side * 17.2f, side * 18.3f), b = std::max(side * 17.2f, side * 18.3f);
        hedge.box(v3(a, 0, -52.6f), v3(b, 0.85f, -36.0f));                       // along the forecourt
        float a2 = std::min(side * 26.0f, side * 46.0f), b2 = std::max(side * 26.0f, side * 46.0f);
        hedge.box(v3(a2, 0, -36.9f), v3(b2, 0.95f, -35.9f));                     // in front of the side lawns
        float a3 = std::min(side * 25.2f, side * 26.6f), b3 = std::max(side * 25.2f, side * 26.6f);
        hedge.box(v3(a3, 0, ZB + 3.0f), v3(b3, 1.2f, ZF + 0.5f));                // along the side walls of the building
        // round shrubs on the lawns next to the walkway
        for (int i = 0; i < 6; i++)
        {
            float x = side * (9.5f + i * 3.4f + rng.range(-0.3f, 0.3f)), z = -14.0f - (i % 2) * 3.0f + rng.range(-0.6f, 0.6f);
            float r = rng.range(0.34f, 0.52f);
            shrubs.blob(v3(x, r * 0.75f, z), v3(r, r * 0.85f, r), 10, 6, 0.06f, rng);
        }
    }

    // ---- brick walls ----
    MeshBuilder brick;
    auto wallRun = [&](float xa, float xb, float z, float h, float pillarEvery) {
        float lo = std::min(xa, xb), hi = std::max(xa, xb);
        brick.box(v3(lo, 0, z - 0.17f), v3(hi, h, z + 0.17f));
        int n = std::max(1, (int)roundf((hi - lo) / pillarEvery));
        for (int i = 0; i <= n; i++)
        {
            float x = lo + (hi - lo) * i / n;
            brick.box(v3(x - 0.30f, 0, z - 0.30f), v3(x + 0.30f, h + 0.35f, z + 0.30f));
        }
    };
    auto wallRunZ = [&](float x, float za, float zb, float h, float pillarEvery) {
        float lo = std::min(za, zb), hi = std::max(za, zb);
        brick.box(v3(x - 0.17f, 0, lo), v3(x + 0.17f, h, hi));
        int n = std::max(1, (int)roundf((hi - lo) / pillarEvery));
        for (int i = 0; i <= n; i++)
        {
            float z = lo + (hi - lo) * i / n;
            brick.box(v3(x - 0.30f, 0, z - 0.30f), v3(x + 0.30f, h + 0.35f, z + 0.30f));
        }
    };
    for (float side = -1.0f; side <= 1.0f; side += 2.0f)
    {
        wallRun(side * 6.0f, side * 74.0f, -19.3f, 0.55f, 5.6f);     // low garden wall between the lawn and the drive
        wallRun(side * 24.6f, side * 80.0f, -98.0f, 2.7f, 6.0f);     // boundary wall behind the lawns
        wallRunZ(side * 80.0f, -98.0f, -4.0f, 2.7f, 6.0f);           // boundary wall along the sides
    }

    // ---- neighbouring buildings ----
    struct Block { float x, z, w, d, h; Color c; };
    const Block BLOCKS[12] = {
        { -57, -130, 22, 18, 28, { 226, 160, 112, 255 } },   // tall peach block on the left (as in the photo)
        { -88, -152, 26, 20, 19, { 224, 208, 172, 255 } },
        { -112, -108, 30, 24, 23, { 214, 196, 186, 255 } },
        { -104, -52, 24, 34, 16, { 226, 214, 184, 255 } },
        { -134, -150, 30, 26, 26, { 205, 150, 110, 255 } },
        { 60, -127, 24, 18, 23, { 204, 208, 214, 255 } },
        { 88, -156, 28, 22, 31, { 230, 172, 124, 255 } },
        { 72, -186, 22, 20, 20, { 226, 214, 184, 255 } },
        { 114, -104, 30, 26, 21, { 216, 200, 168, 255 } },
        { 102, -50, 26, 32, 15, { 196, 200, 206, 255 } },
        { 138, -148, 30, 24, 24, { 222, 206, 176, 255 } },
        { -30, -210, 44, 22, 17, { 212, 204, 190, 255 } } };
    MeshBuilder blocks[12];
    for (int i = 0; i < 12; i++)
    {
        const Block& b = BLOCKS[i];
        blocks[i].box(v3(b.x - b.w * 0.5f, 0, b.z - b.d * 0.5f), v3(b.x + b.w * 0.5f, b.h, b.z + b.d * 0.5f));
        // parapet / stair tower on the roof
        blocks[i].box(v3(b.x - b.w * 0.18f, b.h, b.z - b.d * 0.2f), v3(b.x + b.w * 0.12f, b.h + 3.2f, b.z + b.d * 0.2f));
    }
    // mosque with a white dome and minarets
    MeshBuilder mosque;
    {
        float mx = -73.0f, mz = -168.0f;
        mosque.box(v3(mx - 10, 0, mz - 10), v3(mx + 10, 11.0f, mz + 10));
        mosque.frustum(v3(mx, 11.0f, mz), 6.6f, 6.6f, 2.2f, 20, false);
        Rng r2; r2.s = 5u;
        mosque.blob(v3(mx, 13.6f, mz), v3(6.4f, 6.8f, 6.4f), 20, 12, 0.0f, r2);
        mosque.frustum(v3(mx, 19.8f, mz), 0.25f, 0.05f, 2.6f, 6, false);
        for (int k = 0; k < 4; k++)
        {
            float cx = mx + ((k & 1) ? 10.0f : -10.0f), cz = mz + ((k & 2) ? 10.0f : -10.0f);
            mosque.frustum(v3(cx, 0, cz), 1.15f, 0.85f, 24.0f, 10, false);
            mosque.frustum(v3(cx, 24.0f, cz), 1.5f, 1.5f, 0.5f, 10, true);
            mosque.frustum(v3(cx, 24.5f, cz), 0.8f, 0.7f, 3.0f, 10, false);
            mosque.blob(v3(cx, 28.2f, cz), v3(1.0f, 1.3f, 1.0f), 10, 6, 0.0f, r2);
        }
    }
    // rooftop billboard frame on the right (plain panel, as in the second photo)
    MeshBuilder board;
    board.box(v3(52.0f, 26.5f, -127.3f), v3(68.0f, 33.0f, -126.9f));
    metal.box(v3(54.0f, 23.0f, -127.5f), v3(54.4f, 26.5f, -127.1f));
    metal.box(v3(65.6f, 23.0f, -127.5f), v3(66.0f, 26.5f, -127.1f));

    auto add = [&](MeshBuilder& b, int mat, Color tint) {
        if (b.empty()) return;
        Part p; p.mesh = b.upload(); p.mat = mat; p.tint = tint;
        parts.push_back(p);
    };
    add(wall, MAT_PANEL, WHITE);
    add(glass, MAT_GLASS, WHITE);
    add(granite, MAT_GRANITE, WHITE);
    add(metal, MAT_PLAIN, Color{ 150, 155, 162, 255 });
    add(lamps, MAT_PLAIN, Color{ 240, 240, 232, 255 });
    add(pots, MAT_PLAIN, Color{ 196, 104, 58, 255 });
    add(soil, MAT_PLAIN, Color{ 92, 66, 44, 255 });
    add(leaves, MAT_LEAF, Color{ 62, 110, 44, 255 });
    for (int i = 0; i < 6; i++) add(flags[i], MAT_PLAIN, FLAG_COL[i]);
    add(trunks, MAT_PLAIN, Color{ 96, 74, 56, 255 });
    add(crowns[0], MAT_LEAF, Color{ 42, 82, 34, 255 });
    add(crowns[1], MAT_LEAF, Color{ 52, 92, 36, 255 });
    add(crowns[2], MAT_LEAF, Color{ 62, 98, 40, 255 });
    add(hedge, MAT_LEAF, Color{ 40, 76, 32, 255 });
    add(shrubs, MAT_LEAF, Color{ 46, 84, 36, 255 });
    add(brick, MAT_BRICK, Color{ 190, 104, 62, 255 });
    for (int i = 0; i < 12; i++) add(blocks[i], MAT_FACADE, BLOCKS[i].c);
    add(mosque, MAT_PLAIN, Color{ 238, 236, 228, 255 });
    add(board, MAT_PLAIN, Color{ 52, 92, 160, 255 });

    // ---- the lettering, centred above the glass entrance ----
    Font font = LoadFontEx(fontFile.c_str(), 220, nullptr, 0);
    bool own = font.texture.id != 0 && font.texture.id != GetFontDefault().texture.id;
    Image img = ImageTextEx(own ? font : GetFontDefault(), "The University of Faisalabad", own ? 220.0f : 60.0f, own ? 2.0f : 6.0f, WHITE);
    const int pad = 28;
    ImageResizeCanvas(&img, img.width + pad * 2, img.height + pad * 2, pad, pad, Color{ 255, 255, 255, 0 });
    sign = LoadTextureFromImage(img);
    GenTextureMipmaps(&sign);
    SetTextureFilter(sign, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(sign, TEXTURE_WRAP_CLAMP);
    float aspect = (float)img.width / (float)img.height;
    UnloadImage(img);
    if (own) UnloadFont(font);
    float sw = 17.8f, sh = sw / aspect, sx = 0.0f, sy = (HT + H) * 0.5f - 0.1f;
    signRect = Vector4{ sx - sw * 0.5f, sy - sh * 0.5f, sx + sw * 0.5f, sy + sh * 0.5f };

    // ---- bright daytime lighting, as in the photos ----
    hasEnv = true;
    env = Environment{};
    env.sunDir = Vector3Normalize(v3(-0.52f, 0.68f, 0.52f));
    env.sunColor = v3(1.78f, 1.64f, 1.42f);
    env.skyColor = v3(0.46f, 0.55f, 0.74f);
    env.groundColor = v3(0.40f, 0.37f, 0.32f);
    env.horizonColor = v3(0.52f, 0.72f, 0.98f);
    env.zenithColor = v3(0.10f, 0.30f, 0.76f);
    env.fogDensity = 0.0020f;
    env.exposure = 1.22f;
    loaded = true;
}

void StageDef::draw(Renderer& r)
{
    if (procedural)
    {
        for (const Part& p : parts) r.drawProc(p.mesh, p.mat, p.tint, sign, signRect, glassTop, true);
        return;
    }
    SurfaceParams sp;
    sp.lit = 1.0f;
    sp.saturation = 1.15f;
    r.drawStatic(gpu, world, sp);
}

void StageDef::unload()
{
    for (Part& p : parts) UnloadMesh(p.mesh);
    parts.clear();
    if (sign.id) UnloadTexture(sign);
    sign = Texture2D{};
    gpu.unload();
}
