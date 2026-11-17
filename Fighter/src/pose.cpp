// ============================================================================
//  TUF FIGHTER - pose.cpp
//  Pose blending, inverse kinematics and skinning matrices.
// ============================================================================
#include "pose.h"

// ---------------------------------------------------------------------------
// Pose helpers
// ---------------------------------------------------------------------------
Pose poseLerp(const Pose& a, const Pose& b, float t)
{
    Pose r;
    r.hipsPos = lerp3(a.hipsPos, b.hipsPos, t);
    r.hipsRot = lerp3(a.hipsRot, b.hipsRot, t);
    r.chestRot = lerp3(a.chestRot, b.chestRot, t);
    r.headRot = lerp3(a.headRot, b.headRot, t);
    r.handL = lerp3(a.handL, b.handL, t);   r.handR = lerp3(a.handR, b.handR, t);
    r.elbowL = lerp3(a.elbowL, b.elbowL, t); r.elbowR = lerp3(a.elbowR, b.elbowR, t);
    r.footL = lerp3(a.footL, b.footL, t);   r.footR = lerp3(a.footR, b.footR, t);
    r.kneeL = lerp3(a.kneeL, b.kneeL, t);   r.kneeR = lerp3(a.kneeR, b.kneeR, t);
    r.footPitchL = lerpf(a.footPitchL, b.footPitchL, t); r.footPitchR = lerpf(a.footPitchR, b.footPitchR, t);
    r.footYawL = lerpf(a.footYawL, b.footYawL, t);       r.footYawR = lerpf(a.footYawR, b.footYawR, t);
    r.fistL = lerpf(a.fistL, b.fistL, t);   r.fistR = lerpf(a.fistR, b.fistR, t);
    return r;
}

static Vector3 flipX(Vector3 v) { return v3(-v.x, v.y, v.z); }
static Vector3 flipRot(Vector3 r) { return v3(r.x, -r.y, -r.z); }

Pose poseMirror(const Pose& p)
{
    Pose r;
    r.hipsPos = flipX(p.hipsPos);
    r.hipsRot = flipRot(p.hipsRot);
    r.chestRot = flipRot(p.chestRot);
    r.headRot = flipRot(p.headRot);
    r.handL = flipX(p.handR);   r.handR = flipX(p.handL);
    r.elbowL = flipX(p.elbowR); r.elbowR = flipX(p.elbowL);
    r.footL = flipX(p.footR);   r.footR = flipX(p.footL);
    r.kneeL = flipX(p.kneeR);   r.kneeR = flipX(p.kneeL);
    r.footPitchL = p.footPitchR; r.footPitchR = p.footPitchL;
    r.footYawL = -p.footYawR;    r.footYawR = -p.footYawL;
    r.fistL = p.fistR;           r.fistR = p.fistL;
    return r;
}

void bonesBlend(std::vector<BonePose>& dst, const std::vector<BonePose>& a, const std::vector<BonePose>& b, float t)
{
    size_t n = std::min(a.size(), b.size());
    dst.resize(n);
    for (size_t i = 0; i < n; i++)
    {
        dst[i].pos = lerp3(a[i].pos, b[i].pos, t);
        dst[i].rot = QuaternionSlerp(a[i].rot, b[i].rot, t);
    }
}

// Orientation from (pitch, yaw, roll) in degrees. Yaw (turning) is applied
// first, then roll and pitch around the fixed model axes, so "lean forward"
// always means "towards the opponent" no matter how the body is turned.
static Quaternion eulerQ(Vector3 r)
{
    Quaternion qx = quatAxis(v3(1, 0, 0), r.x);
    Quaternion qy = quatAxis(v3(0, 1, 0), r.y);
    Quaternion qz = quatAxis(v3(0, 0, 1), r.z);
    return qmul(qx, qmul(qz, qy));
}

// ---------------------------------------------------------------------------
// Rig construction
// ---------------------------------------------------------------------------
bool Rig::build(const ModelData& m)
{
    model = &m;
    auto F = [&](const char* n) { return m.findBone(n); };
    hips = F("Hips");
    spine[0] = F("Spine"); spine[1] = F("Spine1"); spine[2] = F("Spine2");
    neck = F("Neck"); head = F("Head");
    const char* sideName[2] = { "Left", "Right" };
    const char* fingerName[5] = { "Index", "Middle", "Ring", "Pinky", "Thumb" };
    for (int s = 0; s < 2; s++)
    {
        std::string p = sideName[s];
        shoulder[s] = F((p + "Shoulder").c_str());
        arm[s] = F((p + "Arm").c_str());
        forearm[s] = F((p + "ForeArm").c_str());
        hand[s] = F((p + "Hand").c_str());
        upleg[s] = F((p + "UpLeg").c_str());
        leg[s] = F((p + "Leg").c_str());
        foot[s] = F((p + "Foot").c_str());
        toe[s] = F((p + "ToeBase").c_str());
        for (int f = 0; f < 5; f++)
        {
            fingers[s][f].clear();
            for (int k = 1; k <= 3; k++)
            {
                int b = F((p + "Hand" + fingerName[f] + std::to_string(k)).c_str());
                if (b >= 0) fingers[s][f].push_back(b);
            }
        }
    }
    valid = hips >= 0 && spine[0] >= 0 && spine[1] >= 0 && spine[2] >= 0 && neck >= 0 && head >= 0;
    for (int s = 0; s < 2; s++)
        valid = valid && shoulder[s] >= 0 && arm[s] >= 0 && forearm[s] >= 0 && hand[s] >= 0 &&
                upleg[s] >= 0 && leg[s] >= 0 && foot[s] >= 0;
    if (!valid) return false;

    // Finger curl axes, derived from the hand shape in the bind pose
    for (int s = 0; s < 2; s++)
    {
        curlAxis[s] = v3(0, 0, s == LEFT ? -1.0f : 1.0f);
        thumbAxis[s] = curlAxis[s];
        const auto& idx = fingers[s][0]; const auto& mid = fingers[s][1]; const auto& pinky = fingers[s][3];
        if (!idx.empty() && !mid.empty() && !pinky.empty())
        {
            Vector3 hp = m.bones[hand[s]].bindPos;
            Vector3 fingerDir = Vector3Normalize(m.bones[mid[0]].bindPos - hp);
            Vector3 across = Vector3Normalize(m.bones[pinky[0]].bindPos - m.bones[idx[0]].bindPos);
            Vector3 palm = Vector3Normalize(Vector3CrossProduct(fingerDir, across));
            if (s == LEFT) palm = Vector3Negate(palm);
            curlAxis[s] = Vector3Normalize(Vector3CrossProduct(fingerDir, palm));
            const auto& th = fingers[s][4];
            if (th.size() >= 2)
            {
                Vector3 td = Vector3Normalize(m.bones[th[1]].bindPos - m.bones[th[0]].bindPos);
                thumbAxis[s] = Vector3Normalize(Vector3CrossProduct(td, palm));
            }
        }
    }
    return true;
}

float Rig::shoulderHeight() const { return model->bones[arm[LEFT]].bindPos.y; }
float Rig::hipHeight() const { return model->bones[hips].bindPos.y; }

// ---------------------------------------------------------------------------
// Two bone IK. Positions the chain a -> b -> c so that c reaches `target`,
// bending towards `pole`. `bindPole` is the direction the middle joint
// (elbow / knee) naturally points in the bind pose.
// ---------------------------------------------------------------------------
static void solveLimb(const std::vector<Bone>& B, std::vector<BonePose>& out, int a, int b, int c,
                      Vector3 target, Vector3 pole, Vector3 bindPole)
{
    Vector3 bA = B[a].bindPos, bB = B[b].bindPos, bC = B[c].bindPos;
    float l1 = Vector3Distance(bA, bB), l2 = Vector3Distance(bB, bC);
    Vector3 A = out[a].pos;
    Vector3 d = target - A;
    float dist = Vector3Length(d);
    Vector3 dir = dist > 1e-5f ? Vector3Scale(d, 1.0f / dist) : v3(0, -1, 0);
    dist = clampf(dist, fabsf(l1 - l2) + 0.01f, (l1 + l2) * 0.998f);

    float cosA = clampf((l1 * l1 + dist * dist - l2 * l2) / (2.0f * l1 * dist), -1.0f, 1.0f);
    float sinA = sqrtf(std::max(0.0f, 1.0f - cosA * cosA));

    Vector3 pp = pole - Vector3Scale(dir, Vector3DotProduct(pole, dir));
    if (Vector3Length(pp) < 1e-4f)
    {
        pp = Vector3CrossProduct(dir, v3(1, 0, 0));
        if (Vector3Length(pp) < 1e-4f) pp = Vector3CrossProduct(dir, v3(0, 0, 1));
    }
    pp = Vector3Normalize(pp);
    Vector3 E = A + Vector3Scale(dir, l1 * cosA) + Vector3Scale(pp, l1 * sinA);
    Vector3 C = A + Vector3Scale(dir, dist);
    Vector3 n = Vector3Normalize(Vector3CrossProduct(dir, pp));    // normal of the bend plane

    auto bindFrame = [&](Vector3 from, Vector3 to) {
        Vector3 x = Vector3Normalize(to - from);
        Vector3 p = bindPole - Vector3Scale(x, Vector3DotProduct(bindPole, x));
        if (Vector3Length(p) < 1e-4f) p = Vector3CrossProduct(x, v3(1, 0, 0));
        p = Vector3Normalize(p);
        Vector3 nn = Vector3CrossProduct(x, p);
        return quatFromBasis(x, p, nn);
    };
    auto poseFrame = [&](Vector3 from, Vector3 to) {
        Vector3 x = Vector3Normalize(to - from);
        Vector3 y = Vector3CrossProduct(n, x);
        return quatFromBasis(x, y, n);
    };

    out[a].rot = qmul(poseFrame(A, E), QuaternionInvert(bindFrame(bA, bB)));
    out[b].pos = E;
    out[b].rot = qmul(poseFrame(E, C), QuaternionInvert(bindFrame(bB, bC)));
    out[c].pos = C;
}

// ---------------------------------------------------------------------------
// Pose -> bones
// ---------------------------------------------------------------------------
void Rig::solve(const Pose& pose, std::vector<BonePose>& out) const
{
    const std::vector<Bone>& B = model->bones;
    size_t n = B.size();
    out.resize(n);
    static thread_local std::vector<unsigned char> set;
    set.assign(n, 0);

    auto place = [&](int b) {
        int p = B[b].parent;
        out[b].pos = out[p].pos + rotate(out[p].rot, B[b].bindPos - B[p].bindPos);
    };

    // --- torso ---
    Quaternion qHips = eulerQ(pose.hipsRot);
    Quaternion qChest = eulerQ(pose.chestRot);
    Quaternion qHead = eulerQ(pose.headRot);
    out[hips].pos = B[hips].bindPos + pose.hipsPos;
    out[hips].rot = qHips;
    set[hips] = 1;
    for (int i = 0; i < 3; i++)
    {
        place(spine[i]);
        out[spine[i]].rot = QuaternionSlerp(qHips, qChest, (i + 1) / 3.0f);
        set[spine[i]] = 1;
    }
    place(neck);
    out[neck].rot = QuaternionSlerp(qChest, qHead, 0.5f);
    set[neck] = 1;
    place(head);
    out[head].rot = qHead;
    set[head] = 1;

    // --- arms ---
    for (int s = 0; s < 2; s++)
    {
        Vector3 target = (s == LEFT) ? pose.handL : pose.handR;
        Vector3 pole = (s == LEFT) ? pose.elbowL : pose.elbowR;
        float fist = (s == LEFT) ? pose.fistL : pose.fistR;

        // the collar bone follows the chest and shrugs a little towards the hand
        place(shoulder[s]);
        Vector3 armRest = out[shoulder[s]].pos + rotate(qChest, B[arm[s]].bindPos - B[shoulder[s]].bindPos);
        Vector3 toHand = target - armRest;
        Vector3 restDir = rotate(qChest, B[arm[s]].bindPos - B[shoulder[s]].bindPos);
        Quaternion shrug = quatIdentity();
        if (Vector3Length(toHand) > 1e-4f && Vector3Length(restDir) > 1e-4f)
        {
            Vector3 want = Vector3Normalize(Vector3Normalize(restDir) + Vector3Scale(Vector3Normalize(toHand), 0.22f));
            shrug = QuaternionFromVector3ToVector3(Vector3Normalize(restDir), want);
        }
        out[shoulder[s]].rot = qmul(shrug, qChest);
        set[shoulder[s]] = 1;
        place(arm[s]);

        solveLimb(B, out, arm[s], forearm[s], hand[s], target, pole, v3(0, 0, -1));
        out[hand[s]].rot = out[forearm[s]].rot;
        set[arm[s]] = set[forearm[s]] = set[hand[s]] = 1;

        // fingers curl into a fist
        static const float curl[3] = { 62.0f, 80.0f, 55.0f };
        for (int f = 0; f < 5; f++)
        {
            bool thumb = (f == 4);
            Quaternion q = out[hand[s]].rot;
            const std::vector<int>& chain = fingers[s][f];
            for (size_t k = 0; k < chain.size(); k++)
            {
                float ang = thumb ? 28.0f : curl[k < 3 ? k : 2];
                q = qmul(q, quatAxis(thumb ? thumbAxis[s] : curlAxis[s], ang * fist));
                place(chain[k]);
                out[chain[k]].rot = q;
                set[chain[k]] = 1;
            }
        }
    }

    // --- legs ---
    for (int s = 0; s < 2; s++)
    {
        Vector3 f = (s == LEFT) ? pose.footL : pose.footR;
        Vector3 pole = (s == LEFT) ? pose.kneeL : pose.kneeR;
        float pitch = (s == LEFT) ? pose.footPitchL : pose.footPitchR;
        float yaw = (s == LEFT) ? pose.footYawL : pose.footYawR;
        place(upleg[s]);
        Vector3 target = v3(f.x, f.y + B[foot[s]].bindPos.y, f.z);
        solveLimb(B, out, upleg[s], leg[s], foot[s], target, pole, v3(0, 0, 1));
        Quaternion qYaw = quatAxis(v3(0, 1, 0), yaw);
        out[foot[s]].rot = qmul(qYaw, quatAxis(v3(1, 0, 0), pitch));
        set[upleg[s]] = set[leg[s]] = set[foot[s]] = 1;
        if (toe[s] >= 0)
        {
            place(toe[s]);
            // when the heel is raised the toes stay flat on the ground
            out[toe[s]].rot = (pitch > 0.0f && f.y < 0.25f) ? qYaw : out[foot[s]].rot;
            set[toe[s]] = 1;
        }
    }

    // --- everything else simply follows its parent ---
    for (int b : model->order)
    {
        if (set[b]) continue;
        int p = B[b].parent;
        if (p < 0) { out[b].pos = B[b].bindPos; out[b].rot = quatIdentity(); }
        else { out[b].rot = out[p].rot; place(b); }
    }
}

// ---------------------------------------------------------------------------
// Sampling of clips stored in the file
// ---------------------------------------------------------------------------
template <typename T, typename LerpFn>
static T sampleTrack(const std::vector<float>& times, const std::vector<T>& values, float t, T fallback, LerpFn lerpFn)
{
    if (times.empty() || values.empty()) return fallback;
    if (t <= times.front()) return values.front();
    if (t >= times.back()) return values[std::min(values.size(), times.size()) - 1];
    size_t hi = std::upper_bound(times.begin(), times.end(), t) - times.begin();
    size_t lo = hi - 1;
    if (hi >= values.size()) return values.back();
    float span = times[hi] - times[lo];
    float k = span > 1e-6f ? (t - times[lo]) / span : 0.0f;
    return lerpFn(values[lo], values[hi], k);
}

void Rig::sampleClip(const FileClip& clip, float time, std::vector<BonePose>& out) const
{
    const std::vector<Bone>& B = model->bones;
    size_t n = B.size();
    out.resize(n);
    static thread_local std::vector<Quaternion> grot;
    grot.resize(n);
    for (int b : model->order)
    {
        const ClipTrack& tr = clip.tracks[b];
        Vector3 T = sampleTrack(tr.tT, tr.T, time, B[b].restT, [](Vector3 a, Vector3 c, float k) { return lerp3(a, c, k); });
        Quaternion R = sampleTrack(tr.tR, tr.R, time, B[b].restR, [](Quaternion a, Quaternion c, float k) { return QuaternionSlerp(a, c, k); });
        int p = B[b].parent;
        if (p < 0) { out[b].pos = T; grot[b] = R; }
        else
        {
            out[b].pos = out[p].pos + rotate(grot[p], T);
            grot[b] = QuaternionNormalize(qmul(grot[p], R));
        }
    }
    for (size_t b = 0; b < n; b++) out[b].rot = qmul(grot[b], QuaternionInvert(B[b].bindRot));
}

// ---------------------------------------------------------------------------
void Rig::skinMatrices(const std::vector<BonePose>& bones, std::vector<Matrix>& out) const
{
    const std::vector<Bone>& B = model->bones;
    size_t n = std::min(B.size(), bones.size());
    out.resize(n);
    for (size_t i = 0; i < n; i++)
    {
        Matrix m = QuaternionToMatrix(bones[i].rot);
        Vector3 t = bones[i].pos - rotate(bones[i].rot, B[i].bindPos);
        m.m12 = t.x; m.m13 = t.y; m.m14 = t.z;
        out[i] = m;
    }
}
