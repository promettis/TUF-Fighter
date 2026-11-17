// ============================================================================
//  TUF FIGHTER - model.cpp
//  .glb loading: geometry, textures, skeleton and embedded animation clips.
// ============================================================================
#include "model.h"

// cgltf is compiled inside raylib (rmodels.c); we only need its declarations.
#include "external/cgltf.h"
#include "meshoptimizer.h"

namespace
{
    Matrix matrixFromColumnMajor(const float* f)
    {
        Matrix m;
        m.m0 = f[0];  m.m1 = f[1];  m.m2 = f[2];   m.m3 = f[3];
        m.m4 = f[4];  m.m5 = f[5];  m.m6 = f[6];   m.m7 = f[7];
        m.m8 = f[8];  m.m9 = f[9];  m.m10 = f[10]; m.m11 = f[11];
        m.m12 = f[12]; m.m13 = f[13]; m.m14 = f[14]; m.m15 = f[15];
        return m;
    }

    // Extract a pure rotation from a matrix that may contain scale
    Quaternion rotationOf(Matrix m)
    {
        Vector3 x = Vector3Normalize(v3(m.m0, m.m1, m.m2));
        Vector3 y = Vector3Normalize(v3(m.m4, m.m5, m.m6));
        Vector3 z = Vector3Normalize(Vector3CrossProduct(x, y));
        y = Vector3CrossProduct(z, x);
        return quatFromBasis(x, y, z);
    }

    const cgltf_accessor* findAttr(const cgltf_primitive& p, cgltf_attribute_type type, int index = 0)
    {
        for (cgltf_size i = 0; i < p.attributes_count; i++)
            if (p.attributes[i].type == type && p.attributes[i].index == index) return p.attributes[i].data;
        return nullptr;
    }

    // Decode an image embedded in the .glb, optionally limiting its size
    Image decodeTexture(const cgltf_texture* tex, int maxSize)
    {
        Image img{};
        if (!tex || !tex->image || !tex->image->buffer_view) return img;
        const cgltf_buffer_view* bv = tex->image->buffer_view;
        const unsigned char* data = (const unsigned char*)bv->buffer->data + bv->offset;
        const char* ext = ".png";
        if (bv->size > 3 && data[0] == 0xFF && data[1] == 0xD8) ext = ".jpg";
        img = LoadImageFromMemory(ext, data, (int)bv->size);
        if (img.data && maxSize > 0 && (img.width > maxSize || img.height > maxSize))
            ImageResize(&img, maxSize, maxSize);
        return img;
    }
}

int ModelData::findBone(const std::string& name) const
{
    for (size_t i = 0; i < bones.size(); i++)
    {
        const std::string& n = bones[i].name;
        if (n == name) return (int)i;
        // accept "mixamorig:Hips" when asked for "Hips"
        size_t colon = n.find_last_of(':');
        if (colon != std::string::npos && n.compare(colon + 1, std::string::npos, name) == 0) return (int)i;
    }
    return -1;
}

void ModelData::loadCpu()
{
    auto fail = [&](const std::string& msg) { error = msg; progress = 1.0f; done = true; };

    // ---- read the whole file ------------------------------------------------
    std::vector<unsigned char> file;
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { fail("Cannot open " + path); return; }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        file.resize((size_t)size);
        size_t got = fread(file.data(), 1, (size_t)size, f);
        fclose(f);
        if (got != (size_t)size) { fail("Cannot read " + path); return; }
    }
    progress = 0.08f;

    cgltf_options opt{};
    cgltf_data* d = nullptr;
    if (cgltf_parse(&opt, file.data(), file.size(), &d) != cgltf_result_success) { fail("Invalid glTF: " + path); return; }
    if (cgltf_load_buffers(&opt, d, path.c_str()) != cgltf_result_success) { cgltf_free(d); fail("Cannot load buffers: " + path); return; }
    progress = 0.12f;

    // ---- geometry -------------------------------------------------------------
    std::vector<float> P, N, UV, W;
    std::vector<unsigned char> J;
    std::vector<unsigned int> I;
    const cgltf_material* material = nullptr;
    const cgltf_skin* skin = d->skins_count ? &d->skins[0] : nullptr;
    skinned = (skin != nullptr);

    for (cgltf_size ni = 0; ni < d->nodes_count; ni++)
    {
        const cgltf_node* node = &d->nodes[ni];
        if (!node->mesh) continue;
        float wm[16];
        cgltf_node_transform_world(node, wm);
        Matrix world = matrixFromColumnMajor(wm);
        bool nodeSkinned = (node->skin != nullptr);

        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; pi++)
        {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* aPos = findAttr(prim, cgltf_attribute_type_position);
            if (!aPos) continue;
            const cgltf_accessor* aNrm = findAttr(prim, cgltf_attribute_type_normal);
            const cgltf_accessor* aUv = findAttr(prim, cgltf_attribute_type_texcoord);
            const cgltf_accessor* aJ = findAttr(prim, cgltf_attribute_type_joints);
            const cgltf_accessor* aW = findAttr(prim, cgltf_attribute_type_weights);
            if (!material && prim.material) material = prim.material;

            size_t base = P.size() / 3, count = aPos->count;
            P.resize((base + count) * 3);
            N.resize((base + count) * 3, 0.0f);
            UV.resize((base + count) * 2, 0.0f);
            cgltf_accessor_unpack_floats(aPos, &P[base * 3], count * 3);
            if (aNrm) cgltf_accessor_unpack_floats(aNrm, &N[base * 3], count * 3);
            if (aUv) cgltf_accessor_unpack_floats(aUv, &UV[base * 2], count * 2);

            if (!nodeSkinned)
            {
                // bake the node transform into static geometry
                for (size_t v = base; v < base + count; v++)
                {
                    Vector3 p = Vector3Transform(v3(P[v * 3], P[v * 3 + 1], P[v * 3 + 2]), world);
                    P[v * 3] = p.x; P[v * 3 + 1] = p.y; P[v * 3 + 2] = p.z;
                    Vector3 n = v3(N[v * 3], N[v * 3 + 1], N[v * 3 + 2]);
                    Vector3 r = v3(world.m0 * n.x + world.m4 * n.y + world.m8 * n.z,
                                   world.m1 * n.x + world.m5 * n.y + world.m9 * n.z,
                                   world.m2 * n.x + world.m6 * n.y + world.m10 * n.z);
                    r = Vector3Normalize(r);
                    N[v * 3] = r.x; N[v * 3 + 1] = r.y; N[v * 3 + 2] = r.z;
                }
            }
            if (skinned)
            {
                J.resize((base + count) * 4, 0);
                W.resize((base + count) * 4, 0.0f);
                if (aJ && aW)
                {
                    for (size_t v = 0; v < count; v++)
                    {
                        cgltf_uint j[4] = { 0, 0, 0, 0 };
                        float w[4] = { 0, 0, 0, 0 };
                        cgltf_accessor_read_uint(aJ, v, j, 4);
                        cgltf_accessor_read_float(aW, v, w, 4);
                        float sum = w[0] + w[1] + w[2] + w[3];
                        if (sum < 1e-6f) { w[0] = 1.0f; sum = 1.0f; }
                        for (int k = 0; k < 4; k++)
                        {
                            J[(base + v) * 4 + k] = (unsigned char)j[k];
                            W[(base + v) * 4 + k] = w[k] / sum;
                        }
                    }
                }
                else
                    for (size_t v = 0; v < count; v++) W[(base + v) * 4] = 1.0f;
            }

            if (prim.indices)
            {
                size_t ib = I.size();
                I.resize(ib + prim.indices->count);
                for (cgltf_size k = 0; k < prim.indices->count; k++)
                    I[ib + k] = (unsigned int)(base + cgltf_accessor_read_index(prim.indices, k));
            }
            else
                for (size_t k = 0; k < count; k++) I.push_back((unsigned int)(base + k));
        }
    }
    if (I.size() < 3) { cgltf_free(d); fail("No triangles in " + path); return; }
    size_t vcount = P.size() / 3;
    srcTriangles = (int)(I.size() / 3);
    progress = 0.22f;

    // bounds
    bmin = v3(1e9f, 1e9f, 1e9f); bmax = v3(-1e9f, -1e9f, -1e9f);
    for (size_t v = 0; v < vcount; v++)
    {
        bmin = Vector3Min(bmin, v3(P[v * 3], P[v * 3 + 1], P[v * 3 + 2]));
        bmax = Vector3Max(bmax, v3(P[v * 3], P[v * 3 + 1], P[v * 3 + 2]));
    }

    // ---- simplify very dense meshes so they render fast on any GPU -----------
    if (targetTriangles > 0 && srcTriangles > targetTriangles)
    {
        std::vector<unsigned int> out(I.size());
        float err = 0.0f;
        size_t n = meshopt_simplify(out.data(), I.data(), I.size(), P.data(), vcount, sizeof(float) * 3,
                                    (size_t)targetTriangles * 3, 0.02f, 0, &err);
        out.resize(n);
        I.swap(out);
    }
    meshopt_optimizeVertexCache(I.data(), I.data(), I.size(), vcount);
    outTriangles = (int)(I.size() / 3);
    progress = 0.40f;

    // ---- split into chunks that fit 16 bit indices ----------------------------
    {
        std::vector<int> stamp(vcount, -1), remap(vcount, 0);
        int chunkId = -1;
        MeshChunk* c = nullptr;
        auto newChunk = [&]() { chunks.emplace_back(); chunkId++; c = &chunks.back(); };
        newChunk();
        for (size_t t = 0; t + 2 < I.size(); t += 3)
        {
            int need = 0;
            for (int k = 0; k < 3; k++) if (stamp[I[t + k]] != chunkId) need++;
            if (c->pos.size() / 3 + need > 65535) newChunk();
            for (int k = 0; k < 3; k++)
            {
                unsigned int v = I[t + k];
                if (stamp[v] != chunkId)
                {
                    stamp[v] = chunkId;
                    remap[v] = (int)(c->pos.size() / 3);
                    c->pos.insert(c->pos.end(), &P[v * 3], &P[v * 3] + 3);
                    c->nrm.insert(c->nrm.end(), &N[v * 3], &N[v * 3] + 3);
                    c->uv.insert(c->uv.end(), &UV[v * 2], &UV[v * 2] + 2);
                    if (skinned)
                    {
                        c->joints.insert(c->joints.end(), &J[v * 4], &J[v * 4] + 4);
                        c->weights.insert(c->weights.end(), &W[v * 4], &W[v * 4] + 4);
                    }
                }
                c->idx.push_back((unsigned short)remap[v]);
            }
        }
    }
    progress = 0.45f;

    // ---- skeleton ---------------------------------------------------------------
    if (skin)
    {
        size_t n = skin->joints_count;
        bones.resize(n);
        std::unordered_map<const cgltf_node*, int> index;
        for (size_t i = 0; i < n; i++) index[skin->joints[i]] = (int)i;
        for (size_t i = 0; i < n; i++)
        {
            const cgltf_node* node = skin->joints[i];
            Bone& b = bones[i];
            b.name = node->name ? node->name : "";
            auto it = index.find(node->parent);
            b.parent = (it != index.end()) ? it->second : -1;
            float m[16];
            if (skin->inverse_bind_matrices && cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16))
                b.invBind = matrixFromColumnMajor(m);
            else
            {
                cgltf_node_transform_world(node, m);
                b.invBind = MatrixInvert(matrixFromColumnMajor(m));
            }
            Matrix g = MatrixInvert(b.invBind);
            b.bindPos = v3(g.m12, g.m13, g.m14);
            b.bindRot = rotationOf(g);
            if (node->has_translation) b.restT = v3(node->translation[0], node->translation[1], node->translation[2]);
            if (node->has_rotation) b.restR = Quaternion{ node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3] };
            if (node->has_scale) b.restS = v3(node->scale[0], node->scale[1], node->scale[2]);
        }
        // evaluation order: parents before children
        std::vector<char> placed(n, 0);
        while (order.size() < n)
        {
            size_t before = order.size();
            for (size_t i = 0; i < n; i++)
            {
                if (placed[i]) continue;
                if (bones[i].parent < 0 || placed[bones[i].parent]) { placed[i] = 1; order.push_back((int)i); }
            }
            if (order.size() == before) break; // malformed hierarchy
        }

        // ---- animation clips stored in the file ---------------------------------
        for (cgltf_size ai = 0; ai < d->animations_count; ai++)
        {
            const cgltf_animation& a = d->animations[ai];
            FileClip clip;
            clip.name = a.name ? a.name : "";
            clip.tracks.resize(n);
            for (cgltf_size ci = 0; ci < a.channels_count; ci++)
            {
                const cgltf_animation_channel& ch = a.channels[ci];
                auto it = index.find(ch.target_node);
                if (it == index.end() || !ch.sampler) continue;
                ClipTrack& tr = clip.tracks[it->second];
                const cgltf_accessor* in = ch.sampler->input;
                const cgltf_accessor* outAcc = ch.sampler->output;
                bool cubic = (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline);
                std::vector<float> times(in->count);
                for (cgltf_size k = 0; k < in->count; k++) cgltf_accessor_read_float(in, k, &times[k], 1);
                if (!times.empty()) clip.duration = std::max(clip.duration, times.back());
                for (cgltf_size k = 0; k < in->count; k++)
                {
                    float v[4] = { 0, 0, 0, 1 };
                    cgltf_size src = cubic ? (k * 3 + 1) : k;
                    if (ch.target_path == cgltf_animation_path_type_translation)
                    { cgltf_accessor_read_float(outAcc, src, v, 3); tr.T.push_back(v3(v[0], v[1], v[2])); }
                    else if (ch.target_path == cgltf_animation_path_type_rotation)
                    { cgltf_accessor_read_float(outAcc, src, v, 4); tr.R.push_back(Quaternion{ v[0], v[1], v[2], v[3] }); }
                    else if (ch.target_path == cgltf_animation_path_type_scale)
                    { cgltf_accessor_read_float(outAcc, src, v, 3); tr.S.push_back(v3(v[0], v[1], v[2])); }
                }
                if (ch.target_path == cgltf_animation_path_type_translation) tr.tT = times;
                else if (ch.target_path == cgltf_animation_path_type_rotation) tr.tR = times;
                else if (ch.target_path == cgltf_animation_path_type_scale) tr.tS = times;
            }
            if (clip.duration > 0.0f) clips.push_back(std::move(clip));
        }
    }
    progress = 0.50f;

    // ---- textures (the slowest part: PNG/JPG decoding) -------------------------
    if (material)
    {
        if (material->has_pbr_metallic_roughness)
        {
            albedo = decodeTexture(material->pbr_metallic_roughness.base_color_texture.texture, 2048);
            progress = 0.75f;
            orm = decodeTexture(material->pbr_metallic_roughness.metallic_roughness_texture.texture, 1024);
            progress = 0.85f;
        }
        normal = decodeTexture(material->normal_texture.texture, 1024);
    }
    cgltf_free(d);
    progress = 1.0f;
    done = true;
}

// ---------------------------------------------------------------------------
// GPU upload (main thread)
// ---------------------------------------------------------------------------
static void* dupMem(const void* src, size_t bytes)
{
    void* p = MemAlloc((unsigned int)bytes);
    memcpy(p, src, bytes);
    return p;
}

static Texture2D makeTexture(Image& img, bool aniso)
{
    Texture2D t{};
    if (!img.data) return t;
    t = LoadTextureFromImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, aniso ? TEXTURE_FILTER_ANISOTROPIC_8X : TEXTURE_FILTER_TRILINEAR);
    UnloadImage(img);
    img = Image{};
    return t;
}

void GpuModel::upload(ModelData& src)
{
    for (MeshChunk& c : src.chunks)
    {
        Mesh m{};
        m.vertexCount = (int)(c.pos.size() / 3);
        m.triangleCount = (int)(c.idx.size() / 3);
        m.vertices = (float*)dupMem(c.pos.data(), c.pos.size() * sizeof(float));
        m.normals = (float*)dupMem(c.nrm.data(), c.nrm.size() * sizeof(float));
        m.texcoords = (float*)dupMem(c.uv.data(), c.uv.size() * sizeof(float));
        m.indices = (unsigned short*)dupMem(c.idx.data(), c.idx.size() * sizeof(unsigned short));
        if (src.skinned)
        {
            m.boneCount = (int)src.bones.size();
            m.boneIndices = (unsigned char*)dupMem(c.joints.data(), c.joints.size());
            m.boneWeights = (float*)dupMem(c.weights.data(), c.weights.size() * sizeof(float));
        }
        UploadMesh(&m, false);
        meshes.push_back(m);
        // the CPU copy is no longer needed
        std::vector<float>().swap(c.pos); std::vector<float>().swap(c.nrm);
        std::vector<float>().swap(c.uv);  std::vector<float>().swap(c.weights);
        std::vector<unsigned char>().swap(c.joints);
        std::vector<unsigned short>().swap(c.idx);
    }
    albedo = makeTexture(src.albedo, !src.skinned);
    normal = makeTexture(src.normal, false);
    orm = makeTexture(src.orm, false);
    ready = true;
}

void GpuModel::unload()
{
    for (Mesh& m : meshes) UnloadMesh(m);
    meshes.clear();
    if (albedo.id) UnloadTexture(albedo);
    if (normal.id) UnloadTexture(normal);
    if (orm.id) UnloadTexture(orm);
    albedo = normal = orm = Texture2D{};
    ready = false;
}

// ---------------------------------------------------------------------------
void ModelLoader::start(ModelData* data)
{
    join();
    th = std::thread([data]() { data->loadCpu(); });
}
void ModelLoader::join()
{
    if (th.joinable()) th.join();
}
