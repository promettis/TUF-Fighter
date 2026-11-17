// ============================================================================
//  TUF FIGHTER - pose.h
//  Procedural character posing.
//
//  The character models ship with only a single "Running" clip, so every
//  fighting animation in the game is generated in code. A `Pose` describes a
//  body position in a rig independent way (where are the hands and feet, how
//  are hips / chest / head oriented). `Rig::solve` turns it into bone
//  transforms using two-bone inverse kinematics for arms and legs, so the
//  same pose works on every character regardless of proportions.
//
//  Model space convention: +Y up, +Z forward (towards the opponent),
//  +X = the character's left. Units are metres (characters are 1.7 m tall).
// ============================================================================
#pragma once
#include "engine.h"
#include "model.h"

struct Pose
{
    Vector3 hipsPos{ 0, 0, 0 };        // hips offset from the rest position
    Vector3 hipsRot{ 0, 0, 0 };        // degrees: x = lean forward, y = turn left, z = roll
    Vector3 chestRot{ 0, 0, 0 };       // absolute orientation of the upper chest (same convention)
    Vector3 headRot{ 0, 0, 0 };        // absolute orientation of the head
    Vector3 handL{ 0.25f, 0.9f, 0.05f }, handR{ -0.25f, 0.9f, 0.05f };   // wrist targets
    Vector3 elbowL{ 0.3f, -0.6f, -1.0f }, elbowR{ -0.3f, -0.6f, -1.0f }; // direction the elbows point
    Vector3 footL{ 0.11f, 0, 0 }, footR{ -0.11f, 0, 0 };                 // ankle targets (y = height above standing)
    Vector3 kneeL{ 0.1f, 0, 1.0f }, kneeR{ -0.1f, 0, 1.0f };            // direction the knees point
    float footPitchL = 0, footPitchR = 0;   // + = heel raised / toes down
    float footYawL = 0, footYawR = 0;       // + = toes turned to the character's left
    float fistL = 1.0f, fistR = 1.0f;       // 0 = open hand, 1 = closed fist
};

Pose poseLerp(const Pose& a, const Pose& b, float t);
Pose poseMirror(const Pose& p);             // left <-> right mirrored version

// Final transform of one bone: position + rotation relative to the bind pose (model space)
struct BonePose
{
    Vector3 pos{};
    Quaternion rot = quatIdentity();
};
void bonesBlend(std::vector<BonePose>& dst, const std::vector<BonePose>& a, const std::vector<BonePose>& b, float t);

enum Side { LEFT = 0, RIGHT = 1 };

struct Rig
{
    const ModelData* model = nullptr;
    int hips = -1, spine[3] = { -1, -1, -1 }, neck = -1, head = -1;
    int shoulder[2], arm[2], forearm[2], hand[2];
    int upleg[2], leg[2], foot[2], toe[2];
    std::vector<int> fingers[2][5];        // per hand: index, middle, ring, pinky, thumb chains
    Vector3 curlAxis[2]{};                 // finger curl axis in bind space
    Vector3 thumbAxis[2]{};
    bool valid = false;

    bool build(const ModelData& m);
    // Pose -> bone transforms
    void solve(const Pose& pose, std::vector<BonePose>& out) const;
    // Sample an animation clip stored in the model file
    void sampleClip(const FileClip& clip, float time, std::vector<BonePose>& out) const;
    // Bone transforms -> skinning matrices for the shader
    void skinMatrices(const std::vector<BonePose>& bones, std::vector<Matrix>& out) const;

    float shoulderHeight() const;
    float hipHeight() const;
};
