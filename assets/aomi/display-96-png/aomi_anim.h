#pragma once

#include "codex_pet_anim.h"

namespace aomi {

static constexpr uint16_t k_idle_durations_ms[] = {1680, 660, 660, 840, 840, 1920};
static constexpr uint16_t k_running_right_durations_ms[] = {120, 120, 120, 120, 120, 120, 120, 220};
static constexpr uint16_t k_running_left_durations_ms[] = {120, 120, 120, 120, 120, 120, 120, 220};
static constexpr uint16_t k_waving_durations_ms[] = {140, 140, 140, 280};
static constexpr uint16_t k_jumping_durations_ms[] = {140, 140, 140, 140, 280};
static constexpr uint16_t k_failed_durations_ms[] = {140, 140, 140, 140, 140, 140, 140, 240};
static constexpr uint16_t k_waiting_durations_ms[] = {150, 150, 150, 150, 150, 260};
static constexpr uint16_t k_running_durations_ms[] = {120, 120, 120, 120, 120, 220};
static constexpr uint16_t k_review_durations_ms[] = {150, 150, 150, 150, 150, 280};

static constexpr codex_pet::StateInfo k_states[] = {
    {"idle", 6, k_idle_durations_ms},
    {"running-right", 8, k_running_right_durations_ms},
    {"running-left", 8, k_running_left_durations_ms},
    {"waving", 4, k_waving_durations_ms},
    {"jumping", 5, k_jumping_durations_ms},
    {"failed", 8, k_failed_durations_ms},
    {"waiting", 6, k_waiting_durations_ms},
    {"running", 6, k_running_durations_ms},
    {"review", 6, k_review_durations_ms},
};

static constexpr codex_pet::PetSpec k_pet = {
    "aomi",
    "Aomi",
    "/aomi/display-96-png",
    96,
    104,
    k_states,
    static_cast<uint8_t>(sizeof(k_states) / sizeof(k_states[0])),
};

}  // namespace aomi
