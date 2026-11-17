// ============================================================================
//  TUF FIGHTER - model.h
//  Loading of .glb models (characters + stages).
//
//  Loading is split in two halves so the loading screen can stay animated:
//    1. ModelData::loadCpu()   - runs on a worker thread: parses the file,
//       decodes the textures, simplifies / splits the mesh, reads the skeleton.
//    2. GpuModel::upload()     - runs on the main thread: creates GPU buffers.
// ============================================================================
#pragma once
#include "engine.h"

// One joint of a skeleton (bind pose, expressed in model space)
struct Bone
{
    std::string name;
    int        parent = -1;
    Vector3    bindPos{};                  // joint position in the bind pose (model space)
    Quaternion bindRot = quatIdentity();   // joint orientation in the bind pose (model space)
    Matrix     invBind = MatrixIdentity(); // inverse bind matrix from the file
    // rest local transform (used when sampling animation clips stored in the file)
    Vector3    restT{};
    Quaternion restR = quatIdentity();
    Vector3    restS{ 1, 1, 1 };
};

// Keyframed animation clip stored inside the .glb file
struct ClipTrack
{
    std::vector<float> tT, tR, tS;
    std::vector<Vector3> T, S;
    std::vector<Quaternion> R;
};
struct FileClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<ClipTrack> tracks;         // one per bone
    bool valid() const { return duration > 0.0f && !tracks.empty(); }
};

// A piece of geometry small enough for 16 bit indices
struct MeshChunk
{
    std::vector<float> pos, nrm, uv, weights;
    std::vector<unsigned char> joints;
    std::vector<unsigned short> idx;
};

// CPU side model, filled by the worker thread
struct ModelData
{
    std::string path;
    std::string error;
    int   targetTriangles = 0;             // 0 = keep the original triangle count
    bool  skinned = false;

    std::vector<MeshChunk> chunks;
    Image albedo{}, normal{}, orm{};
    std::vector<Bone> bones;
    std::vector<int>  order;               // bone evaluation order (parents first)
    std::vector<FileClip> clips;
    Vector3 bmin{}, bmax{};
    int srcTriangles = 0, outTriangles = 0;

    std::atomic<float> progress{ 0.0f };   // 0..1, read by the loading screen
    std::atomic<bool>  done{ false };

    void loadCpu();                        // heavy work, thread safe
    int  findBone(const std::string& name) const;
};

// GPU side model
struct GpuModel
{
    std::vector<Mesh> meshes;
    Texture2D albedo{}, normal{}, orm{};
    bool ready = false;

    void upload(ModelData& src);           // main thread only; frees the CPU images
    void unload();
};

// Launches ModelData::loadCpu on a background thread
class ModelLoader
{
public:
    void start(ModelData* data);
    void join();
    ~ModelLoader() { join(); }
private:
    std::thread th;
};
