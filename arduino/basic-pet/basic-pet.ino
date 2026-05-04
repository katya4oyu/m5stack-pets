#include <Arduino.h>
#include <FS.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <SPIFFS.h>

#include "aomi_anim.h"
#include "bitomos_umi_anim.h"
#include "codex_pet_anim.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static constexpr int k_screen_width = 320;
static constexpr int k_screen_height = 240;
static constexpr int k_footer_height = 48;
static constexpr int k_pet_width = 96;
static constexpr int k_pet_height = 104;
static constexpr int k_pet_x = (k_screen_width - k_pet_width) / 2;
static constexpr int k_pet_y = (k_screen_height - k_footer_height - k_pet_height) / 2;
static constexpr int k_footer_y = k_screen_height - k_footer_height;
static constexpr uint32_t k_tick_ms = 16;
static constexpr uint32_t k_bg_color = 0x101418;
static constexpr uint32_t k_footer_color = 0x1c2228;
static constexpr uint32_t k_text_color = 0xdce4ec;

struct PetEntry {
  const char* shortName;
  const codex_pet::PetSpec* spec;
  codex_pet::Player player;
};

struct StateFrameCache {
  size_t petIndex = SIZE_MAX;
  uint8_t stateIndex = UINT8_MAX;
  uint8_t frameCount = 0;
  uint16_t* pixels = nullptr;
  size_t pixelCapacity = 0;
};

static PetEntry g_pets[] = {
    {"Aomi", &aomi::k_pet, {}},
    {"Bitomos", &bitomos_umi::k_pet, {}},
};

static size_t g_current_pet_index = 0;
static StateFrameCache g_state_cache;
static M5Canvas g_decode_canvas(&M5.Display);
static bool g_spiffs_ready = false;
static bool g_decode_canvas_ready = false;

static uint32_t now_ms()
{
  return millis();
}

static PetEntry& current_pet()
{
  return g_pets[g_current_pet_index];
}

static const codex_pet::PetSpec& current_spec()
{
  return *current_pet().spec;
}

static codex_pet::Player& current_player()
{
  return current_pet().player;
}

static uint16_t rgb_to_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
  return static_cast<uint16_t>(((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3));
}

static uint16_t bg_rgb565()
{
  return rgb_to_rgb565(16, 20, 24);
}

static void* alloc_bytes(size_t bytes)
{
  void* ptr = ps_malloc(bytes);
  if (!ptr) {
    ptr = malloc(bytes);
  }
  return ptr;
}

static bool make_png_frame_path(char* out, size_t outSize, const codex_pet::PetSpec& spec, uint8_t stateIndex, uint8_t frameIndex)
{
  const codex_pet::StateInfo* state = codex_pet::stateInfo(spec, stateIndex);
  if (!out || outSize == 0 || !state || frameIndex >= state->frameCount) {
    return false;
  }

  const int written = snprintf(
      out,
      outSize,
      "/p%u/s%u/%02u.png",
      static_cast<unsigned>(g_current_pet_index),
      static_cast<unsigned>(stateIndex),
      static_cast<unsigned>(frameIndex));
  return written > 0 && static_cast<size_t>(written) < outSize;
}

static bool read_file(const char* path, uint8_t** outData, size_t* outSize)
{
  *outData = nullptr;
  *outSize = 0;

  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    Serial.printf("open failed: %s\n", path);
    return false;
  }

  const size_t fileSize = file.size();
  if (fileSize == 0) {
    Serial.printf("empty file: %s\n", path);
    file.close();
    return false;
  }

  uint8_t* data = static_cast<uint8_t*>(alloc_bytes(fileSize));
  if (!data) {
    Serial.printf("png allocation failed: %s size=%u\n", path, static_cast<unsigned>(fileSize));
    file.close();
    return false;
  }

  const size_t bytesRead = file.read(data, fileSize);
  file.close();
  if (bytesRead != fileSize) {
    Serial.printf("read failed: %s read=%u size=%u\n", path, static_cast<unsigned>(bytesRead), static_cast<unsigned>(fileSize));
    free(data);
    return false;
  }

  *outData = data;
  *outSize = fileSize;
  return true;
}

static bool ensure_state_cache_capacity(uint8_t frameCount)
{
  const size_t neededPixels = static_cast<size_t>(frameCount) * k_pet_width * k_pet_height;
  if (g_state_cache.pixelCapacity >= neededPixels) {
    return true;
  }

  uint16_t* pixels = static_cast<uint16_t*>(alloc_bytes(neededPixels * sizeof(uint16_t)));
  if (!pixels) {
    Serial.printf("state cache allocation failed: frames=%u\n", static_cast<unsigned>(frameCount));
    return false;
  }

  free(g_state_cache.pixels);
  g_state_cache.pixels = pixels;
  g_state_cache.pixelCapacity = neededPixels;
  return true;
}

static bool decode_png_to_cache(const char* path, uint16_t* dst)
{
  uint8_t* png = nullptr;
  size_t pngSize = 0;
  if (!read_file(path, &png, &pngSize)) {
    return false;
  }

  g_decode_canvas.fillSprite(bg_rgb565());
  const bool decoded = g_decode_canvas.drawPng(png, pngSize, 0, 0, k_pet_width, k_pet_height);
  free(png);
  if (!decoded) {
    Serial.printf("png decode failed: %s\n", path);
    return false;
  }

  const void* buffer = g_decode_canvas.getBuffer();
  if (!buffer) {
    Serial.println("decode canvas has no buffer");
    return false;
  }

  memcpy(dst, buffer, k_pet_width * k_pet_height * sizeof(uint16_t));
  return true;
}

static void draw_message(const char* line1, const char* line2 = nullptr)
{
  M5.Display.fillRect(0, 0, k_screen_width, k_footer_y, k_bg_color);
  M5.Display.setTextColor(k_text_color, k_bg_color);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(line1, k_screen_width / 2, k_footer_y / 2 - 8);
  if (line2) {
    M5.Display.drawString(line2, k_screen_width / 2, k_footer_y / 2 + 10);
  }
  M5.Display.setTextDatum(top_left);
}

static bool load_current_state_cache()
{
  if (!g_spiffs_ready) {
    draw_message("SPIFFS is not mounted");
    return false;
  }
  if (!g_decode_canvas_ready) {
    draw_message("decode canvas allocation failed");
    return false;
  }

  const codex_pet::PetSpec& spec = current_spec();
  const codex_pet::Player& player = current_player();
  const codex_pet::StateInfo* state = codex_pet::stateInfo(spec, player.stateIndex);
  if (!state || state->frameCount == 0) {
    draw_message("invalid animation state");
    return false;
  }

  if (g_state_cache.petIndex == g_current_pet_index &&
      g_state_cache.stateIndex == player.stateIndex &&
      g_state_cache.frameCount == state->frameCount) {
    return true;
  }

  if (spec.frameWidth != k_pet_width || spec.frameHeight != k_pet_height) {
    draw_message("unsupported frame size");
    return false;
  }

  if (!ensure_state_cache_capacity(state->frameCount)) {
    draw_message("state cache allocation failed");
    return false;
  }

  Serial.printf("loading state cache: pet=%s state=%s frames=%u\n", spec.petId, state->name, static_cast<unsigned>(state->frameCount));
  for (uint8_t frame = 0; frame < state->frameCount; ++frame) {
    char path[128] = {};
    if (!make_png_frame_path(path, sizeof(path), spec, player.stateIndex, frame)) {
      return false;
    }

    uint16_t* dst = g_state_cache.pixels + static_cast<size_t>(frame) * k_pet_width * k_pet_height;
    if (!decode_png_to_cache(path, dst)) {
      g_state_cache.petIndex = SIZE_MAX;
      g_state_cache.stateIndex = UINT8_MAX;
      g_state_cache.frameCount = 0;
      draw_message("PNG asset not found", path);
      return false;
    }
  }

  g_state_cache.petIndex = g_current_pet_index;
  g_state_cache.stateIndex = player.stateIndex;
  g_state_cache.frameCount = state->frameCount;
  return true;
}

static void draw_footer()
{
  constexpr int leftWidth = k_screen_width / 3;
  constexpr int centerWidth = k_screen_width / 3;
  constexpr int rightWidth = k_screen_width - leftWidth - centerWidth;
  M5.Display.fillRect(0, k_footer_y, k_screen_width, k_footer_height, k_footer_color);
  M5.Display.setTextColor(0xffffff, k_footer_color);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("<", leftWidth / 2, k_footer_y + k_footer_height / 2);
  M5.Display.drawString("*", leftWidth + centerWidth / 2, k_footer_y + k_footer_height / 2);
  M5.Display.drawString(">", leftWidth + centerWidth + rightWidth / 2, k_footer_y + k_footer_height / 2);
  M5.Display.setTextDatum(top_left);
}

static void draw_status()
{
  char text[64] = {};
  snprintf(text, sizeof(text), "%s | %s", current_pet().shortName, codex_pet::stateName(current_spec(), current_player().stateIndex));

  M5.Display.fillRect(0, 0, k_screen_width, 24, k_bg_color);
  M5.Display.setTextColor(k_text_color, k_bg_color);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.drawString(text, 8, 6);
}

static void render_current_frame()
{
  if (!load_current_state_cache()) {
    draw_footer();
    return;
  }

  const codex_pet::Player& player = current_player();
  if (player.frameIndex >= g_state_cache.frameCount) {
    return;
  }

  const uint16_t* pixels = g_state_cache.pixels + static_cast<size_t>(player.frameIndex) * k_pet_width * k_pet_height;
  M5.Display.startWrite();
  M5.Display.pushImage(k_pet_x, k_pet_y, k_pet_width, k_pet_height, pixels);
  M5.Display.endWrite();
  draw_status();
}

static void set_state_index(uint8_t stateIndex)
{
  if (codex_pet::setState(current_player(), stateIndex, now_ms())) {
    render_current_frame();
  }
}

static void set_state_name(const char* stateName)
{
  if (codex_pet::setState(current_player(), stateName, now_ms())) {
    render_current_frame();
  }
}

static void previous_state()
{
  const codex_pet::PetSpec& spec = current_spec();
  const uint8_t current = current_player().stateIndex;
  const uint8_t next = current == 0 ? static_cast<uint8_t>(spec.stateCount - 1) : static_cast<uint8_t>(current - 1);
  set_state_index(next);
}

static void next_state()
{
  const codex_pet::PetSpec& spec = current_spec();
  const uint8_t next = static_cast<uint8_t>((current_player().stateIndex + 1) % spec.stateCount);
  set_state_index(next);
}

static void switch_pet()
{
  const uint8_t stateIndex = current_player().stateIndex;
  g_current_pet_index = (g_current_pet_index + 1) % (sizeof(g_pets) / sizeof(g_pets[0]));
  current_pet().player = codex_pet::makePlayer(current_spec(), stateIndex, now_ms());
  render_current_frame();
}

static void handle_touch(int32_t x, int32_t y)
{
  if (y >= k_footer_y) {
    if (x < k_screen_width / 3) {
      previous_state();
    } else if (x < (k_screen_width * 2) / 3) {
      switch_pet();
    } else {
      next_state();
    }
    return;
  }

  if (x >= k_pet_x && x < k_pet_x + k_pet_width && y >= k_pet_y && y < k_pet_y + k_pet_height) {
    set_state_name("waving");
  }
}

static void poll_input()
{
  M5.update();

  if (M5.BtnA.wasClicked()) {
    previous_state();
  }
  if (M5.BtnB.wasClicked() || M5.BtnB.wasHold()) {
    switch_pet();
  }
  if (M5.BtnC.wasClicked()) {
    next_state();
  }

  const auto touch = M5.Touch.getDetail();
  if (touch.wasClicked()) {
    handle_touch(touch.x, touch.y);
  }
}

static void create_ui()
{
  M5.Display.fillScreen(k_bg_color);
  draw_footer();

  g_decode_canvas.setColorDepth(16);
  g_decode_canvas.setPsram(true);
  g_decode_canvas_ready = g_decode_canvas.createSprite(k_pet_width, k_pet_height) != nullptr;
}

}  // namespace

void setup()
{
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setBrightness(128);
  M5.Display.setRotation(1);

  g_spiffs_ready = SPIFFS.begin(false);
  if (!g_spiffs_ready) {
    Serial.println("SPIFFS mount failed");
  }

  for (auto& pet : g_pets) {
    pet.player = codex_pet::makePlayer(*pet.spec, 0, now_ms());
  }

  create_ui();
  render_current_frame();
  current_player().dirty = false;
}

void loop()
{
  poll_input();
  if (codex_pet::update(current_player(), now_ms())) {
    render_current_frame();
    current_player().dirty = false;
  }
  delay(k_tick_ms);
}
