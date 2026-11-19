#pragma once
#include <cstdint>

// Input grammar is independent of equipment and of the previous fight. Values
// here name audited native records; contact and damage remain native work.
namespace rxroutes {
enum class Direction { Neutral, Toward, Away, Left, Right };
enum class Action { Light = 1, Technique = 2, Rush = 3, Burst = 4, Flurry = 5 };
enum class Role { Pressure, Advance, Sweep, Launch, Carry, Descend, Flurry };
struct Context {
    int group = 0;
    int kind = 0;
    unsigned pressure = 0;
    int previous_id = -1;
};
struct Route {
    int id;
    Role role;
    bool ending = false;
    bool launcher = false;
};

constexpr bool signature(Action action) { return action >= Action::Rush; }
constexpr Action finisher(Direction direction) {
    return direction == Direction::Toward ? Action::Rush :
           direction == Direction::Away ? Action::Burst : Action::Flurry;
}
constexpr Direction relative_direction(int movement, int target_heading, int cone_degrees = 45) {
    if (movement < 0) return Direction::Neutral;
    // Directions are quantized once at the press, in target/world space. Keep
    // side commands available even with the forgiving forward/back profile.
    const int degrees = cone_degrees < 30 ? 30 : cone_degrees > 60 ? 60 : cone_degrees;
    const int half_width = degrees * 65536 / 360;
    const int difference = static_cast<std::int16_t>((movement - target_heading) & 0xffff);
    if (difference >= -half_width && difference <= half_width) return Direction::Toward;
    if (difference > 32768 - half_width || difference < -(32768 - half_width)) return Direction::Away;
    return difference > 0 ? Direction::Left : Direction::Right;
}
constexpr Route choose(Context context, Action action, Direction direction) {
    if (context.kind == 19) {
        // This uses the actual r2 graph's six clips, never normal ro records.
        if (context.group) {
            if (action == Action::Technique || signature(action)) return {5, Role::Descend, true};
            return {context.pressure % 2 ? 1040 : 1030, Role::Carry};
        }
        if (action == Action::Technique || signature(action)) return {2, Role::Sweep, true};
        return {context.pressure % 2 ? 1010 : 1001, Role::Pressure};
    }
    if (action == Action::Light || signature(action)) {
        if (context.group) {
            constexpr int air[] = {3, 11, 12};
            return {air[context.pressure % 3], Role::Carry};
        }
        constexpr int ground[] = {0, 1060, 5070, 1};
        return {ground[context.pressure % 4], Role::Pressure};
    }
    if (context.group) {
        switch (direction) {
        case Direction::Toward: return {4101, Role::Carry};
        case Direction::Away: return {13151, Role::Descend, true};
        case Direction::Left: return {11041, Role::Descend, true};
        case Direction::Right: return {5, Role::Descend, true};
        default: return {13140, Role::Descend, true};
        }
    }
    switch (direction) {
    case Direction::Toward:
        return {context.previous_id == 1 ? 13090 : 1, Role::Advance};
    case Direction::Away:
        return context.previous_id == 13080 ? Route{10120, Role::Carry} :
               Route{13080, Role::Launch, false, true};
    case Direction::Left: return {6061, Role::Pressure};
    case Direction::Right: return {5130, Role::Flurry, true};
    default: return {14021, Role::Sweep};
    }
}
// These values are animation frames, not engine update ticks. A clean hit can
// sacrifice later contacts, while a miss retains the authored contact tail.
constexpr int hit_cancel_delay(Role role) {
    return role == Role::Flurry ? 3 : role == Role::Descend ? 2 : 1;
}
constexpr int recovery_frames(Role role) {
    return role == Role::Flurry ? 3 : role == Role::Descend ? 2 : 1;
}
}
