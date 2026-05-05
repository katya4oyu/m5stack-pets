#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace codex_pet {

struct StateInfo {
  const char* name;
  uint8_t frameCount;
  const uint16_t* durationsMs;
};

struct PetSpec {
  const char* petId;
  const char* displayName;
  const char* assetRoot;
  uint16_t frameWidth;
  uint16_t frameHeight;
  const StateInfo* states;
  uint8_t stateCount;
};

struct Player {
  const PetSpec* spec;
  uint8_t stateIndex;
  uint8_t frameIndex;
  uint8_t completedLoops;
  uint32_t frameStartedAtMs;
  bool dirty;
};

inline const StateInfo* stateInfo(const PetSpec& spec, uint8_t stateIndex) {
  if (!spec.states || stateIndex >= spec.stateCount) {
    return nullptr;
  }
  return &spec.states[stateIndex];
}

inline const char* stateName(const PetSpec& spec, uint8_t stateIndex) {
  const StateInfo* info = stateInfo(spec, stateIndex);
  return info ? info->name : "";
}

inline uint8_t frameCount(const PetSpec& spec, uint8_t stateIndex) {
  const StateInfo* info = stateInfo(spec, stateIndex);
  return info ? info->frameCount : 0;
}

inline uint16_t frameDurationMs(const PetSpec& spec, uint8_t stateIndex, uint8_t frameIndex) {
  const StateInfo* info = stateInfo(spec, stateIndex);
  if (!info || frameIndex >= info->frameCount) {
    return 0;
  }
  return info->durationsMs[frameIndex];
}

inline bool findStateIndex(const PetSpec& spec, const char* stateName, uint8_t* outIndex) {
  if (!stateName) {
    return false;
  }
  for (uint8_t index = 0; index < spec.stateCount; ++index) {
    const StateInfo* info = stateInfo(spec, index);
    if (info && strcmp(info->name, stateName) == 0) {
      if (outIndex) {
        *outIndex = index;
      }
      return true;
    }
  }
  return false;
}

inline uint8_t nextFrame(const PetSpec& spec, uint8_t stateIndex, uint8_t frameIndex) {
  const uint8_t count = frameCount(spec, stateIndex);
  if (count == 0) {
    return 0;
  }
  return static_cast<uint8_t>((frameIndex + 1) % count);
}

inline bool isIdleState(const PetSpec& spec, uint8_t stateIndex) {
  const char* name = stateName(spec, stateIndex);
  return strcmp(name, "idle") == 0;
}

inline Player makePlayer(const PetSpec& spec, uint8_t initialStateIndex = 0, uint32_t nowMs = 0) {
  Player player = {&spec, initialStateIndex, 0, 0, nowMs, true};
  if (initialStateIndex >= spec.stateCount) {
    player.stateIndex = 0;
  }
  return player;
}

inline bool setState(Player& player, uint8_t stateIndex, uint32_t nowMs) {
  if (!player.spec || stateIndex >= player.spec->stateCount) {
    return false;
  }
  if (player.stateIndex == stateIndex && player.frameIndex == 0) {
    return true;
  }
  player.stateIndex = stateIndex;
  player.frameIndex = 0;
  player.completedLoops = 0;
  player.frameStartedAtMs = nowMs;
  player.dirty = true;
  return true;
}

inline bool setState(Player& player, const char* stateName, uint32_t nowMs) {
  if (!player.spec) {
    return false;
  }
  uint8_t stateIndex = 0;
  if (!findStateIndex(*player.spec, stateName, &stateIndex)) {
    return false;
  }
  return setState(player, stateIndex, nowMs);
}

inline bool update(Player& player, uint32_t nowMs) {
  if (!player.spec) {
    return false;
  }
  const uint16_t duration = frameDurationMs(*player.spec, player.stateIndex, player.frameIndex);
  if (duration == 0 || static_cast<uint32_t>(nowMs - player.frameStartedAtMs) < duration) {
    return false;
  }
  const uint8_t next = nextFrame(*player.spec, player.stateIndex, player.frameIndex);
  if (next == 0 && !isIdleState(*player.spec, player.stateIndex)) {
    player.completedLoops = static_cast<uint8_t>(player.completedLoops + 1);
    if (player.completedLoops >= 3) {
      uint8_t idleStateIndex = 0;
      if (findStateIndex(*player.spec, "idle", &idleStateIndex)) {
        player.stateIndex = idleStateIndex;
        player.frameIndex = 0;
        player.completedLoops = 0;
        player.frameStartedAtMs = nowMs;
        player.dirty = true;
        return true;
      }
    }
  }
  player.frameIndex = next;
  player.frameStartedAtMs = nowMs;
  player.dirty = true;
  return true;
}

inline bool makeFramePath(
    char* out,
    size_t outSize,
    const PetSpec& spec,
    uint8_t stateIndex,
    uint8_t frameIndex) {
  const StateInfo* info = stateInfo(spec, stateIndex);
  if (!out || outSize == 0 || !spec.assetRoot || !info || frameIndex >= info->frameCount) {
    return false;
  }

  const int written = snprintf(
      out,
      outSize,
      "%s/%s/%02u.png",
      spec.assetRoot,
      info->name,
      static_cast<unsigned>(frameIndex));
  return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace codex_pet
