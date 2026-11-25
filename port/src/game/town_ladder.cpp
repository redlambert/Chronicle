#include "town_ladder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <vector>

#include "editloop.hpp"
#include "snd.hpp"

namespace dc::game::expanded {

namespace {

constexpr float kPi = 3.14159265358979f;

constexpr int kXiao = 1;
constexpr int kGoro = 2;
constexpr int kRuby = 3;
constexpr int kUngaga = 4;
constexpr int kOsmond = 5;

// Playback flags (CCharacter::motion_flags): 0 loops, 2 plays once and holds the last frame.
constexpr int kLoop = 0;
constexpr int kOnce = 2;

// The town slots the moves use (town_moveset.cpp).
constexpr int kIdle = 0;
constexpr int kWalk = 2;
constexpr int kRefuse = 7;
constexpr int kFall = 8;
constexpr int kLand = 9;

// TownLadder.cs's tuning, per frame at 60 frames a second.
constexpr float kWalkSpeed = 0.30f;
constexpr int   kAlignMin = 8;
constexpr int   kAlignMax = 60;
constexpr float kLandMargin = 1.0f;
constexpr int   kMinFrames = 20;
constexpr int   kMaxFrames = 90;

constexpr float kGravity = 0.10f;
constexpr float kDownHop = 1.5f;
constexpr float kApexOver = 8.0f;
constexpr float kBackSpeed = 0.20f;
constexpr float kBackDist = 6.0f;
constexpr int   kReadyHold = 30;
constexpr int   kXiaoReady = 11;
constexpr int   kXiaoFloatUp = 12;
constexpr int   kXiaoWalkBack = 13;

constexpr int   kOzJumpDown = 11;
constexpr int   kOzPropeller = 12;
constexpr int   kOzStartFly = 13;
constexpr int   kOzFlyLoop = 14;
constexpr int   kOzRevStartFly = 15;
constexpr int   kOzStow = 16;
constexpr float kHeliRise = 0.5f;
constexpr float kHeliDescend = 0.125f;
constexpr float kHeliForward = 0.30f;
constexpr float kHeliHover = 4.0f;
constexpr float kHeliPlayback = 0.5f;
constexpr int   kHeliDeployFrames = 112;
constexpr int   kHeliStartFrames = 30;
constexpr int   kHeliStowFrames = 116;
constexpr int   kHeliMaxRise = 400;
constexpr float kOzLandSpeed = 0.30f;
constexpr int   kOzLandFrames = 50;

constexpr int   kGoroHopA = 11;
constexpr int   kGoroHopB = 12;
constexpr int   kGoroRevLand = 13;
constexpr int   kGoroHops = 4;
constexpr int   kGoroHopMaxT = 31;
constexpr int   kGoroHopMinT = 12;
constexpr float kGoroHopEndFall = 0.5f;
constexpr float kGoroGravity = 0.15f;
constexpr int   kGoroHopSe = 361;
constexpr int   kGoroKickFrames = 20;
constexpr float kGoroFinalOver = 15.0f;
constexpr float kGoroFinalKickMax = 1.0f;
constexpr int   kGoroFinalLandLead = 8;
constexpr float kGoroDownHop = 2.1f;
constexpr int   kGoroLandCycle = 40;
constexpr int   kGoroLandClip = 10;
constexpr float kGoroWindRev = 0.40f;
constexpr float kGoroWindFwd = 0.80f;

constexpr float kRubyRise = 0.35f;
constexpr float kRubyForward = 0.30f;
constexpr float kRubySettle = 0.15f;
constexpr float kRubyDescend = 0.35f;
constexpr float kRubyHover = 3.0f;
constexpr float kRubyEntrySpeed = 0.03f;
constexpr int   kRubyEntry = 24;
constexpr float kRubyEntryLift = 0.08f;
constexpr int   kRubyRamp = 8;

// Toan's ladder camera (gedit/system/event.stb label 1): close in at the ladder's facing.
constexpr float kCamDistance = 30.0f;
constexpr float kCamHeight = 50.0f;

// One step of a move, played over its frames.
struct Step {
    int   frames = 1;
    int   motion = -1; // the clip started as the step begins, or -1 to keep the one playing
    int   flags = kOnce;
    float speed = -1.0f; // -1 plays at the clip's own KEY speed
    float dx = 0.0f;     // per frame
    float dy = 0.0f;
    float dz = 0.0f;
    float dyaw = 0.0f;

    std::optional<float> face; // facing snapped to as the step ends

    // A ballistic arc: y += vy, vy -= g each frame.
    bool                 arc = false;
    std::optional<float> vy; // launch speed, else the speed the last arc left
    float                g = 0.0f;
    int                  air_motion = -1; // switched to past the arc's top
    int                  air_flags = kOnce;
    int                  land_motion = -1; // started land_lead frames before the arc ends
    int                  land_flags = kOnce;
    float                land_speed = -1.0f;
    int                  land_lead = 0;

    std::optional<std::array<float, 3>> snap; // position set as the step begins
    int                                 se = -1;
};

struct Move {
    std::vector<Step> steps;
    float             ladder_yaw = 0.0f;
    bool              airborne_end = false; // hand back falling, for the game's own landing
};

struct State {
    bool  active = false;
    Move  move;
    int   step = 0;
    int   frame = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float vy = 0.0f;
    int   motion = -1;
    float camera_distance = 0.0f;
    float camera_height = 0.0f;
};

State g_state;

float Wrap(float a) {
    while (a > kPi) {
        a -= 2.0f * kPi;
    }

    while (a < -kPi) {
        a += 2.0f * kPi;
    }

    return a;
}

float Dist2(const float *a, float x, float z) {
    return (a[0] - x) * (a[0] - x) + (a[2] - z) * (a[2] - z);
}

// What every move starts from: where the character stands, the ladder's two ends and its facing.
struct Ladder {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float yaw0 = 0.0f;
    float target[3] = {};
    float mount[3] = {};
    float h = 0.0f;
    float ladder_yaw = 0.0f;
    float face = 0.0f;
    int   align = 0;
};

std::optional<Ladder> ReadLadder(CCharacter *chara, const ED_EVENT_PARAM *param) {
    Ladder        l;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR rot;
    chara->GetPosition(pos);
    chara->GetRotation(rot);
    l.px = pos[0];
    l.py = pos[1];
    l.pz = pos[2];
    l.yaw0 = rot[1];

    const float *a = param->position;
    const float *b = param->camera_pos;
    const bool   a_far = Dist2(a, l.px, l.pz) >= Dist2(b, l.px, l.pz);
    const float *target = a_far ? a : b;
    const float *mount = a_far ? b : a;

    for (int i = 0; i < 3; i++) {
        l.target[i] = target[i];
        l.mount[i] = mount[i];
    }

    l.h = l.target[1] + kLandMargin - l.py;

    if (std::abs(l.h) < 2.0f || std::abs(l.h) > 500.0f) {
        return std::nullopt;
    }

    l.ladder_yaw = param->rotation[1];
    l.face = l.h < 0.0f ? std::atan2(l.target[0] - l.mount[0], l.target[2] - l.mount[2]) : l.ladder_yaw;
    const float walk = std::sqrt(Dist2(l.mount, l.px, l.pz));
    l.align = std::clamp(static_cast<int>(walk / kWalkSpeed), kAlignMin, kAlignMax);
    return l;
}

// Walks to the foot of the move, turning to face it.
Step Align(const Ladder &l) {
    const float frames = static_cast<float>(l.align);
    return Step{.frames = l.align,
                .motion = kWalk,
                .flags = kLoop,
                .dx = (l.mount[0] - l.px) / frames,
                .dz = (l.mount[2] - l.pz) / frames,
                .dyaw = Wrap(l.face - l.yaw0) / frames,
                .face = l.face};
}

Step Hold(int frames, int motion, int flags = kOnce, float speed = -1.0f) {
    return Step{.frames = std::max(1, frames), .motion = motion, .flags = flags, .speed = speed};
}

Step Glide(int frames, float dx, float dy, float dz) {
    return Step{.frames = std::max(1, frames), .dx = dx, .dy = dy, .dz = dz};
}

Step Snap(const Ladder &l) {
    return Step{.frames = 1, .snap = std::array<float, 3>{l.target[0], l.target[1], l.target[2]}};
}

int ArcFrames(float vy0, float g, float drop) {
    const int frames = static_cast<int>((vy0 + std::sqrt(vy0 * vy0 + 2.0f * g * drop)) / g);
    return std::clamp(frames, kMinFrames, kMaxFrames);
}

Move Refusal(int ally) {
    const int frames = ally == kXiao ? 175 : ally == kGoro ? 65 : ally == kRuby ? 105 : 190;
    return Move{.steps = {Hold(frames, kRefuse), Hold(1, kIdle, kLoop)}};
}

// Xiao: walk to the ladder, back up a few steps at the bottom, crouch, then leap over and drop.
Move XiaoMove(const Ladder &l) {
    Move m{.ladder_yaw = l.ladder_yaw, .airborne_end = true};
    m.steps.push_back(Align(l));

    const bool  up = l.h > 0.0f;
    const float fsin = std::sin(l.face);
    const float fcos = std::cos(l.face);
    const float back = up ? kBackDist : 0.0f;
    const float sx = l.mount[0] - fsin * back;
    const float sz = l.mount[2] - fcos * back;

    if (up) {
        const int frames = static_cast<int>(std::ceil(back / kBackSpeed));
        m.steps.push_back(Step{.frames = frames, .motion = kXiaoWalkBack, .flags = kLoop, .dx = -fsin * kBackSpeed, .dz = -fcos * kBackSpeed});
    }

    m.steps.push_back(Hold(kReadyHold, kXiaoReady, kLoop));

    float vy0 = 0.0f;
    int   frames = 0;

    if (up) {
        vy0 = std::sqrt(2.0f * kGravity * (l.h + kApexOver));
        frames = std::clamp(static_cast<int>(std::ceil(vy0 / kGravity + std::sqrt(2.0f * kApexOver / kGravity))), kMinFrames, kMaxFrames);
    } else {
        vy0 = kDownHop;
        frames = ArcFrames(vy0, kGravity, -l.h);
    }

    m.steps.push_back(Step{.frames = frames,
                           .motion = up ? kXiaoFloatUp : kFall,
                           .flags = kOnce,
                           .dx = (l.target[0] - sx) / static_cast<float>(frames),
                           .dz = (l.target[2] - sz) / static_cast<float>(frames),
                           .arc = true,
                           .vy = vy0,
                           .g = kGravity,
                           .air_motion = kFall,
                           .air_flags = kOnce});
    return m;
}

// Osmond: down is a dive into his falling loop and a land; up is the helicopter backpack.
Move OsmondMove(const Ladder &l) {
    Move m{.ladder_yaw = l.ladder_yaw};
    m.steps.push_back(Align(l));

    if (l.h < 0.0f) {
        const float drop = l.py - l.target[1];
        const int   frames = ArcFrames(kDownHop, kGravity, drop);
        const int   lead = std::min(12, frames / 3);
        m.steps.push_back(Step{.frames = frames,
                               .motion = kOzJumpDown,
                               .flags = kOnce,
                               .dx = (l.target[0] - l.mount[0]) / static_cast<float>(frames),
                               .dz = (l.target[2] - l.mount[2]) / static_cast<float>(frames),
                               .arc = true,
                               .vy = kDownHop,
                               .g = kGravity,
                               .air_motion = kFall,
                               .air_flags = kLoop,
                               .land_motion = kLand,
                               .land_flags = kOnce,
                               .land_speed = kOzLandSpeed,
                               .land_lead = lead});
        m.steps.push_back(Snap(l));
        m.steps.push_back(Hold(std::max(kOzLandFrames - lead, 8), -1));
        return m;
    }

    const int   rise = std::clamp(static_cast<int>((l.h - kLandMargin + kHeliHover) / kHeliRise), kMinFrames, kHeliMaxRise);
    const float fx = l.target[0] - l.mount[0];
    const float fz = l.target[2] - l.mount[2];
    int         forward = std::max(static_cast<int>(std::sqrt(fx * fx + fz * fz) / kHeliForward), 8);
    // Whole loops of the fly clip, so the hand-off to the descent lands on the same spin phase.
    forward += (10 - (rise + forward) % 10) % 10;
    const int down = std::max(static_cast<int>(kHeliHover / kHeliDescend), 8);

    m.steps.push_back(Hold(kHeliDeployFrames, kOzPropeller, kOnce, kHeliPlayback));
    m.steps.push_back(Hold(kHeliStartFrames, kOzStartFly, kOnce, kHeliPlayback));
    m.steps.push_back(Step{.frames = rise, .motion = kOzFlyLoop, .flags = kLoop, .speed = kHeliPlayback, .dy = kHeliRise});
    m.steps.push_back(Glide(forward, fx / static_cast<float>(forward), 0.0f, fz / static_cast<float>(forward)));
    m.steps.push_back(Step{.frames = down, .motion = kOzRevStartFly, .flags = kOnce, .speed = kHeliPlayback, .dy = -kHeliDescend});
    m.steps.push_back(Snap(l));
    m.steps.push_back(Hold(kHeliStowFrames, kOzStow, kOnce, kHeliPlayback));
    return m;
}

// Goro: down is a gathered jump off into his falling loop; up is four hops, the last one carrying
// him onto the ledge, as his treehouse climb does.
Move GoroMove(const Ladder &l) {
    Move m{.ladder_yaw = l.ladder_yaw};
    m.steps.push_back(Align(l));

    if (l.h < 0.0f) {
        const float drop = l.py - l.target[1];
        const int   frames = ArcFrames(kGoroDownHop, kGoroGravity, drop);
        const int   lead = std::min(12, frames / 3);
        m.steps.push_back(Hold(static_cast<int>(kGoroLandClip / kGoroWindRev), kGoroRevLand, kOnce, kGoroWindRev));
        m.steps.push_back(Hold(static_cast<int>(kGoroLandClip / kGoroWindFwd) + 2, kLand, kOnce, kGoroWindFwd));
        m.steps.push_back(Step{.frames = frames,
                               .motion = kFall,
                               .flags = kLoop,
                               .dx = (l.target[0] - l.mount[0]) / static_cast<float>(frames),
                               .dz = (l.target[2] - l.mount[2]) / static_cast<float>(frames),
                               .arc = true,
                               .vy = kGoroDownHop,
                               .g = kGoroGravity,
                               .air_motion = kFall,
                               .air_flags = kLoop,
                               .land_motion = kLand,
                               .land_flags = kOnce,
                               .land_lead = lead});
        m.steps.push_back(Snap(l));
        m.steps.push_back(Hold(std::max(kGoroLandCycle - lead, 8), -1));
        return m;
    }

    // Each hop's length is solved so its arc crests late and settles on the rung.
    const float climb = l.h - kLandMargin;
    const float dy = climb / static_cast<float>(kGoroHops);
    int         n = kGoroHopMinT;

    while (n < kGoroHopMaxT && kGoroGravity * static_cast<float>(n + 1) / 2.0f - dy / static_cast<float>(n) < kGoroHopEndFall) {
        n++;
    }

    const float v0 = dy / static_cast<float>(n) + kGoroGravity * static_cast<float>(n - 1) / 2.0f;
    const float kick = static_cast<float>(kGoroKickFrames) / static_cast<float>(kGoroHopMaxT);

    for (int i = 0; i < kGoroHops - 1; i++) {
        m.steps.push_back(Step{.frames = n,
                               .motion = i % 2 == 0 ? kGoroHopA : kGoroHopB,
                               .flags = kOnce,
                               .speed = kick,
                               .arc = true,
                               .vy = v0,
                               .g = kGoroGravity,
                               .se = kGoroHopSe});

        if (n < kGoroHopMaxT) {
            m.steps.push_back(Hold(kGoroHopMaxT - n, -1));
        }
    }

    // The last hop: a taller arc over the ledge, the kick paced to fill it, the land crouch leading
    // the touchdown.
    const float final_rise = (l.target[1] - l.py) / static_cast<float>(kGoroHops);
    const float v0_last = std::sqrt(2.0f * kGoroGravity * (final_rise + kGoroFinalOver));
    int         last = 0;

    for (float yy = 0.0f, vv = v0_last; (vv > 0.0f || yy > final_rise) && last < 90; last++) {
        yy += vv;
        vv -= kGoroGravity;
    }

    last = std::max(last, 1);
    const int   tail = std::min(kGoroFinalLandLead, last);
    const float final_kick = std::min(kGoroFinalKickMax, static_cast<float>(kGoroKickFrames) / static_cast<float>(std::max(last - tail, 1)));
    const float fx = (l.target[0] - l.mount[0]) / static_cast<float>(last);
    const float fz = (l.target[2] - l.mount[2]) / static_cast<float>(last);

    if (last - tail > 0) {
        m.steps.push_back(Step{.frames = last - tail,
                               .motion = (kGoroHops - 1) % 2 == 0 ? kGoroHopA : kGoroHopB,
                               .flags = kOnce,
                               .speed = final_kick,
                               .dx = fx,
                               .dz = fz,
                               .arc = true,
                               .vy = v0_last,
                               .g = kGoroGravity,
                               .se = kGoroHopSe});
    }

    if (tail > 0) {
        m.steps.push_back(Step{.frames = tail, .motion = kLand, .flags = kOnce, .dx = fx, .dz = fz, .arc = true, .g = kGoroGravity});
    }

    m.steps.push_back(Snap(l));
    m.steps.push_back(Hold(50, kLand));
    return m;
}

// Ruby: she floats, up the ladder's line, over the ledge and down onto it, or out over the edge and
// down.
Move RubyMove(const Ladder &l) {
    Move m{.ladder_yaw = l.ladder_yaw};
    m.steps.push_back(Align(l));

    const float fx = l.target[0] - l.mount[0];
    const float fz = l.target[2] - l.mount[2];
    const int   forward = std::max(static_cast<int>(std::sqrt(fx * fx + fz * fz) / kRubyForward), 8);
    const int   settle = std::max(static_cast<int>(kRubyHover / kRubySettle), 8);
    const float step_x = fx / static_cast<float>(forward);
    const float step_z = fz / static_cast<float>(forward);

    if (l.h > 0.0f) {
        const float pre_rise = kRubyEntry * kRubySettle + kRubyRamp * 0.25f;
        const int   rise = std::max(static_cast<int>((l.h - kLandMargin + kRubyHover - pre_rise) / kRubyRise), 4);
        m.steps.push_back(Step{.frames = kRubyEntry, .motion = kFall, .flags = kLoop, .speed = kRubyEntrySpeed, .dy = kRubySettle});
        m.steps.push_back(Step{.frames = kRubyRamp, .motion = kFall, .flags = kLoop, .dy = 0.25f});
        m.steps.push_back(Glide(rise, 0.0f, kRubyRise, 0.0f));
        m.steps.push_back(Glide(forward, step_x, 0.0f, step_z));
        m.steps.push_back(Glide(settle, 0.0f, -kRubySettle, 0.0f));
    } else {
        const float drop = l.py + kRubyEntry * kRubyEntryLift - (l.target[1] + kRubyHover);
        const int   glide = std::max(static_cast<int>(drop / kRubyDescend), 4);
        m.steps.push_back(Step{.frames = kRubyEntry, .motion = kFall, .flags = kLoop, .speed = kRubyEntrySpeed, .dy = kRubyEntryLift});
        m.steps.push_back(Step{.frames = forward, .motion = kFall, .flags = kLoop, .dx = step_x, .dz = step_z});
        m.steps.push_back(Glide(glide, 0.0f, -kRubyDescend, 0.0f));
        m.steps.push_back(Glide(settle, 0.0f, -kRubySettle, 0.0f));
    }

    m.steps.push_back(Snap(l));
    m.steps.push_back(Hold(20, kIdle, kLoop));
    return m;
}

void Play(CCharacter *chara, int motion, int flags, float speed) {
    chara->SetMotion(motion, flags, speed);
    g_state.motion = motion;
}

void Finish(CCharacter *chara, CCameraFollow *camera) {
    g_state.active = false;

    if (camera != nullptr && g_state.camera_distance > 0.0f) {
        camera->SetDistance(g_state.camera_distance);
        camera->SetHeight(g_state.camera_height);
    }

    if (g_state.move.airborne_end) {
        // Xiao ends just above the ledge, still falling: the game's own landing takes her from there.
        chara->SetVelocity(CVector3_f_(0.0f, std::min(g_state.vy, -1.5f), 0.0f));
    } else {
        chara->SetVelocity(CVector3_f_(0.0f, 0.0f, 0.0f));
        chara->SetMotion(kIdle, kLoop);
    }

    std::println("[Ladder] move done");
}

} // namespace

bool TownLadderStart(CCharacter *chara, int ally, const ED_EVENT_PARAM *param, CCameraFollow *camera) {
    if (chara == nullptr || param == nullptr || g_state.active) {
        return g_state.active;
    }

    const std::optional<Ladder> ladder = ReadLadder(chara, param);
    Move                        move;

    if (ally == kUngaga || !ladder) {
        move = Refusal(ally);
    } else if (ally == kXiao) {
        move = XiaoMove(*ladder);
    } else if (ally == kOsmond) {
        move = OsmondMove(*ladder);
    } else if (ally == kGoro) {
        move = GoroMove(*ladder);
    } else if (ally == kRuby) {
        move = RubyMove(*ladder);
    } else {
        return false;
    }

    sceVu0FVECTOR pos;
    sceVu0FVECTOR rot;
    chara->GetPosition(pos);
    chara->GetRotation(rot);
    g_state = State{.active = true, .move = std::move(move), .x = pos[0], .y = pos[1], .z = pos[2], .yaw = rot[1]};

    // A refusal keeps the camera as it is; a move takes Toan's ladder camera.
    if (ladder && ally != kUngaga && camera != nullptr) {
        g_state.camera_distance = camera->GetDistance();
        g_state.camera_height = camera->GetHeight();
        camera->SetDistance(kCamDistance);
        camera->SetHeight(kCamHeight);
        camera->SetAngleSoon(g_state.move.ladder_yaw - kPi);
    }

    int total = 0;

    for (const Step &s : g_state.move.steps) {
        total += s.frames;
    }

    std::println("[Ladder] ally {} {}: {} steps, {} frames{}", ally, !ladder || ally == kUngaga ? "refuses" : ladder->h > 0.0f ? "goes up" : "goes down",
                 g_state.move.steps.size(), total, ladder ? std::format(", height {:.1f}", ladder->h) : std::string(", not a usable ladder"));
    return true;
}

bool TownLadderStep(CCharacter *chara, CCameraFollow *camera) {
    if (!g_state.active || chara == nullptr) {
        return false;
    }

    State &st = g_state;

    if (st.step >= static_cast<int>(st.move.steps.size())) {
        Finish(chara, camera);
        return false;
    }

    const Step &s = st.move.steps[st.step];

    if (st.frame == 0) {
        if (s.snap) {
            st.x = (*s.snap)[0];
            st.y = (*s.snap)[1];
            st.z = (*s.snap)[2];
        }

        if (s.motion >= 0) {
            Play(chara, s.motion, s.flags, s.speed);
        }

        if (s.vy) {
            st.vy = *s.vy;
        }

        if (s.se >= 0) {
            SndSePlay(s.se, -1, 0);
        }
    }

    st.x += s.dx;
    st.z += s.dz;
    st.yaw += s.dyaw;

    if (s.arc) {
        st.y += st.vy;
        st.vy -= s.g;

        if (s.air_motion >= 0 && st.vy < 0.0f && st.motion != s.air_motion && st.motion != s.land_motion) {
            Play(chara, s.air_motion, s.air_flags, -1.0f);
        }

        if (s.land_motion >= 0 && s.land_lead > 0 && s.frames - st.frame <= s.land_lead && st.motion != s.land_motion) {
            Play(chara, s.land_motion, s.land_flags, s.land_speed);
        }
    } else {
        st.y += s.dy;
    }

    chara->SetPosition(st.x, st.y, st.z);
    chara->SetRotation(0.0f, st.yaw, 0.0f);
    chara->SetVelocity(CVector3_f_(0.0f, 0.0f, 0.0f));

    if (camera != nullptr) {
        camera->FollowOn();
        camera->SetFollow(st.x, st.y + 14.0f, st.z);
    }

    st.frame++;

    if (st.frame >= s.frames) {
        if (s.face) {
            st.yaw = *s.face;
            chara->SetRotation(0.0f, st.yaw, 0.0f);
        }

        st.step++;
        st.frame = 0;

        if (st.step >= static_cast<int>(st.move.steps.size())) {
            Finish(chara, camera);
        }
    }

    return true;
}

void TownLadderCancel() {
    if (g_state.active) {
        std::println("[Ladder] move dropped");
    }

    g_state = State{};
}

} // namespace dc::game::expanded
