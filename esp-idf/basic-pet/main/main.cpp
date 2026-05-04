#include <M5GFX.h>
#include <M5Unified.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aomi_anim.h"
#include "bitomos_umi_anim.h"
#include "codex_pet_anim.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static const char* TAG = "basic-pet";

static constexpr int k_screen_width = 320;
static constexpr int k_screen_height = 240;
static constexpr int k_footer_height = 48;
static constexpr int k_pet_width = 96;
static constexpr int k_pet_height = 104;
static constexpr int k_pet_x = (k_screen_width - k_pet_width) / 2;
static constexpr int k_pet_y = (k_screen_height - k_footer_height - k_pet_height) / 2;
static constexpr int k_footer_y = k_screen_height - k_footer_height;
static constexpr char k_spiffs_base_path[] = "/spiffs";
static constexpr char k_png_asset_dir[] = "display-96-png";
static constexpr uint32_t k_tick_ms = 16;
static constexpr uint32_t k_bg_color = 0x101418;
static constexpr uint32_t k_footer_color = 0x1c2228;
static constexpr uint32_t k_text_color = 0xdce4ec;

struct PetEntry {
    const char* short_name;
    const codex_pet::PetSpec* spec;
    codex_pet::Player player;
};

struct StateFrameCache {
    size_t pet_index = SIZE_MAX;
    uint8_t state_index = UINT8_MAX;
    uint8_t frame_count = 0;
    uint16_t* pixels = nullptr;
    size_t pixel_capacity = 0;
};

static PetEntry g_pets[] = {
    {"Aomi", &aomi::k_pet, {}},
    {"Bitomos", &bitomos_umi::k_pet, {}},
};

static size_t g_current_pet_index = 0;
static StateFrameCache g_state_cache;
static M5Canvas g_decode_canvas(&M5.Display);

static uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
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

static esp_err_t mount_spiffs()
{
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = k_spiffs_base_path;
    conf.partition_label = "storage";
    conf.max_files = 8;
    conf.format_if_mount_failed = false;

    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    ESP_RETURN_ON_ERROR(esp_spiffs_info("storage", &total, &used), TAG, "spiffs info failed");
    ESP_LOGI(TAG, "spiffs mounted: total=%u used=%u", static_cast<unsigned>(total), static_cast<unsigned>(used));
    return ESP_OK;
}

static bool make_png_frame_path(char* out, size_t out_size, const codex_pet::PetSpec& spec, uint8_t state_index, uint8_t frame_index)
{
    const codex_pet::StateInfo* state = codex_pet::stateInfo(spec, state_index);
    if (!out || out_size == 0 || !state || frame_index >= state->frameCount) {
        return false;
    }

    const int written = snprintf(
        out,
        out_size,
        "%s/%s/%s/%s/%02u.png",
        k_spiffs_base_path,
        spec.petId,
        k_png_asset_dir,
        state->name,
        static_cast<unsigned>(frame_index));
    return written > 0 && static_cast<size_t>(written) < out_size;
}

static bool read_file(const char* path, uint8_t** out_data, size_t* out_size)
{
    *out_data = nullptr;
    *out_size = 0;

    FILE* file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "open failed: %s", path);
        return false;
    }

    bool ok = fseek(file, 0, SEEK_END) == 0;
    const long file_size = ok ? ftell(file) : -1;
    ok = ok && file_size > 0 && fseek(file, 0, SEEK_SET) == 0;
    if (!ok) {
        fclose(file);
        ESP_LOGE(TAG, "size failed: %s", path);
        return false;
    }

    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(file_size), MALLOC_CAP_8BIT));
    if (!data) {
        fclose(file);
        ESP_LOGE(TAG, "png allocation failed: %s size=%ld", path, file_size);
        return false;
    }

    ok = fread(data, 1, static_cast<size_t>(file_size), file) == static_cast<size_t>(file_size);
    fclose(file);
    if (!ok) {
        heap_caps_free(data);
        ESP_LOGE(TAG, "read failed: %s", path);
        return false;
    }

    *out_data = data;
    *out_size = static_cast<size_t>(file_size);
    return true;
}

static bool ensure_state_cache_capacity(uint8_t frame_count)
{
    const size_t needed_pixels = static_cast<size_t>(frame_count) * k_pet_width * k_pet_height;
    if (g_state_cache.pixel_capacity >= needed_pixels) {
        return true;
    }

    uint16_t* pixels = static_cast<uint16_t*>(heap_caps_malloc(
        needed_pixels * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) {
        pixels = static_cast<uint16_t*>(heap_caps_malloc(needed_pixels * sizeof(uint16_t), MALLOC_CAP_8BIT));
    }
    if (!pixels) {
        ESP_LOGE(TAG, "state cache allocation failed: frames=%u", static_cast<unsigned>(frame_count));
        return false;
    }

    if (g_state_cache.pixels) {
        heap_caps_free(g_state_cache.pixels);
    }
    g_state_cache.pixels = pixels;
    g_state_cache.pixel_capacity = needed_pixels;
    return true;
}

static bool decode_png_to_cache(const char* path, uint16_t* dst)
{
    uint8_t* png = nullptr;
    size_t png_size = 0;
    if (!read_file(path, &png, &png_size)) {
        return false;
    }

    g_decode_canvas.fillSprite(bg_rgb565());
    g_decode_canvas.drawPng(png, png_size, 0, 0, k_pet_width, k_pet_height);
    heap_caps_free(png);

    const void* buffer = g_decode_canvas.getBuffer();
    if (!buffer) {
        ESP_LOGE(TAG, "decode canvas has no buffer");
        return false;
    }

    memcpy(dst, buffer, k_pet_width * k_pet_height * sizeof(uint16_t));
    return true;
}

static bool load_current_state_cache()
{
    const codex_pet::PetSpec& spec = current_spec();
    const codex_pet::Player& player = current_player();
    const codex_pet::StateInfo* state = codex_pet::stateInfo(spec, player.stateIndex);
    if (!state || state->frameCount == 0) {
        ESP_LOGE(TAG, "invalid state: pet=%s state=%u", spec.petId, static_cast<unsigned>(player.stateIndex));
        return false;
    }

    if (g_state_cache.pet_index == g_current_pet_index &&
        g_state_cache.state_index == player.stateIndex &&
        g_state_cache.frame_count == state->frameCount) {
        return true;
    }

    if (spec.frameWidth != k_pet_width || spec.frameHeight != k_pet_height) {
        ESP_LOGE(TAG, "unsupported frame size: %ux%u", spec.frameWidth, spec.frameHeight);
        return false;
    }

    if (!ensure_state_cache_capacity(state->frameCount)) {
        return false;
    }

    ESP_LOGI(
        TAG,
        "loading state cache: pet=%s state=%s frames=%u",
        spec.petId,
        state->name,
        static_cast<unsigned>(state->frameCount));

    for (uint8_t frame = 0; frame < state->frameCount; ++frame) {
        char path[160] = {};
        if (!make_png_frame_path(path, sizeof(path), spec, player.stateIndex, frame)) {
            return false;
        }
        uint16_t* dst = g_state_cache.pixels + static_cast<size_t>(frame) * k_pet_width * k_pet_height;
        if (!decode_png_to_cache(path, dst)) {
            g_state_cache.pet_index = SIZE_MAX;
            g_state_cache.state_index = UINT8_MAX;
            g_state_cache.frame_count = 0;
            return false;
        }
    }

    g_state_cache.pet_index = g_current_pet_index;
    g_state_cache.state_index = player.stateIndex;
    g_state_cache.frame_count = state->frameCount;
    return true;
}

static void draw_footer()
{
    constexpr int left_width = k_screen_width / 3;
    constexpr int center_width = k_screen_width / 3;
    constexpr int right_width = k_screen_width - left_width - center_width;
    M5.Display.fillRect(0, k_footer_y, k_screen_width, k_footer_height, k_footer_color);
    M5.Display.setTextColor(0xffffff, k_footer_color);
    M5.Display.setTextSize(2);
    M5.Display.drawCenterString("<", left_width / 2, k_footer_y + 16);
    M5.Display.drawCenterString("*", left_width + center_width / 2, k_footer_y + 16);
    M5.Display.drawCenterString(">", left_width + center_width + right_width / 2, k_footer_y + 16);
}

static void draw_status()
{
    char text[64] = {};
    snprintf(
        text,
        sizeof(text),
        "%s | %s",
        current_pet().short_name,
        codex_pet::stateName(current_spec(), current_player().stateIndex));

    M5.Display.fillRect(0, 0, k_screen_width, 24, k_bg_color);
    M5.Display.setTextColor(k_text_color, k_bg_color);
    M5.Display.setTextSize(1);
    M5.Display.drawString(text, 8, 6);
}

static void render_current_frame()
{
    if (!load_current_state_cache()) {
        return;
    }

    const codex_pet::Player& player = current_player();
    if (player.frameIndex >= g_state_cache.frame_count) {
        return;
    }

    const uint16_t* pixels = g_state_cache.pixels + static_cast<size_t>(player.frameIndex) * k_pet_width * k_pet_height;
    M5.Display.startWrite();
    M5.Display.pushImage(k_pet_x, k_pet_y, k_pet_width, k_pet_height, pixels);
    M5.Display.endWrite();
    draw_status();
}

static void set_state_index(uint8_t state_index)
{
    if (codex_pet::setState(current_player(), state_index, now_ms())) {
        render_current_frame();
    }
}

static void set_state_name(const char* state_name)
{
    if (codex_pet::setState(current_player(), state_name, now_ms())) {
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
    const uint8_t state_index = current_player().stateIndex;
    g_current_pet_index = (g_current_pet_index + 1) % (sizeof(g_pets) / sizeof(g_pets[0]));
    current_pet().player = codex_pet::makePlayer(current_spec(), state_index, now_ms());
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
    if (!g_decode_canvas.createSprite(k_pet_width, k_pet_height)) {
        ESP_LOGE(TAG, "decode canvas allocation failed");
    }
}

static void animation_task(void*)
{
    while (true) {
        poll_input();
        if (codex_pet::update(current_player(), now_ms())) {
            render_current_frame();
            current_player().dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(k_tick_ms));
    }
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting CoreS3-Lite basic-pet with M5Unified/M5GFX");

    ESP_ERROR_CHECK(mount_spiffs());

    for (auto& pet : g_pets) {
        pet.player = codex_pet::makePlayer(*pet.spec, 0, now_ms());
    }

    M5.begin();
    M5.Display.setBrightness(128);
    M5.Display.setRotation(1);

    create_ui();
    render_current_frame();
    current_player().dirty = false;

    xTaskCreate(animation_task, "pet_anim", 8192, nullptr, 5, nullptr);
}
