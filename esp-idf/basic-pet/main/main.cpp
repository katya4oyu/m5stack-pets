#include <M5GFX.h>
#include <M5Unified.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

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
static constexpr char k_sdcard_base_path[] = "/sdcard";
static constexpr char k_sd_asset_root[] = "assets";
static constexpr char k_png_asset_dir[] = "display-96-png";
static constexpr gpio_num_t k_sd_pin_miso = GPIO_NUM_35;
static constexpr gpio_num_t k_sd_pin_mosi = GPIO_NUM_37;
static constexpr gpio_num_t k_sd_pin_clk = GPIO_NUM_36;
static constexpr gpio_num_t k_sd_pin_cs = GPIO_NUM_4;
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
    size_t failed_pet_index = SIZE_MAX;
    uint8_t failed_state_index = UINT8_MAX;
};

static PetEntry g_pets[] = {
    {"Aomi", &aomi::k_pet, {}},
    {"Bitomos", &bitomos_umi::k_pet, {}},
};

static size_t g_current_pet_index = 0;
static StateFrameCache g_state_cache;
static M5Canvas g_decode_canvas(&M5.Display);
static sdmmc_card_t* g_sd_card = nullptr;
static bool g_sd_ready = false;
static bool g_message_visible = false;
static char g_last_message_line1[64] = {};
static char g_last_message_line2[192] = {};
static char g_last_status_text[64] = {};

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

static esp_err_t mount_sdcard()
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 8;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    const spi_host_device_t spi_host = static_cast<spi_host_device_t>(host.slot);
    spi_bus_config_t bus_config = {};
    bus_config.miso_io_num = k_sd_pin_miso;
    bus_config.mosi_io_num = k_sd_pin_mosi;
    bus_config.sclk_io_num = k_sd_pin_clk;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = 4000;

    esp_err_t err = spi_bus_initialize(spi_host, &bus_config, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "sd spi bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "sd spi bus already initialized, continuing");
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = k_sd_pin_cs;
    slot_config.host_id = spi_host;

    err = esp_vfs_fat_sdspi_mount(k_sdcard_base_path, &host, &slot_config, &mount_config, &g_sd_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sdcard mount failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "sdcard mounted at %s", k_sdcard_base_path);
    sdmmc_card_print_info(stdout, g_sd_card);
    return ESP_OK;
}

static void mount_asset_storage()
{
    g_sd_ready = mount_sdcard() == ESP_OK;
    if (!g_sd_ready) {
        ESP_LOGE(TAG, "sdcard asset storage not mounted");
    }
}

static bool make_sd_png_frame_path(char* out, size_t out_size, const codex_pet::PetSpec& spec, uint8_t state_index, uint8_t frame_index)
{
    const codex_pet::StateInfo* state = codex_pet::stateInfo(spec, state_index);
    if (!out || out_size == 0 || !state || frame_index >= state->frameCount) {
        return false;
    }

    const int written = snprintf(
        out,
        out_size,
        "%s/%s/%s/%s/%s/%02u.png",
        k_sdcard_base_path,
        k_sd_asset_root,
        spec.petId,
        k_png_asset_dir,
        state->name,
        static_cast<unsigned>(frame_index));
    return written > 0 && static_cast<size_t>(written) < out_size;
}

static bool read_asset_file(const char* path, uint8_t** out_data, size_t* out_size)
{
    *out_data = nullptr;
    *out_size = 0;

    FILE* file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "asset not found: %s", path);
        return false;
    }

    bool ok = fseek(file, 0, SEEK_END) == 0;
    const long file_size = ok ? ftell(file) : -1;
    ok = ok && file_size > 0 && fseek(file, 0, SEEK_SET) == 0;
    if (!ok) {
        fclose(file);
        ESP_LOGW(TAG, "size failed: %s", path);
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
        ESP_LOGW(TAG, "read failed: %s", path);
        return false;
    }

    *out_data = data;
    *out_size = static_cast<size_t>(file_size);
    ESP_LOGI(TAG, "asset loaded from sdcard: %s", path);
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
    if (!read_asset_file(path, &png, &png_size)) {
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

static void draw_message(const char* line1, const char* line2 = nullptr)
{
    const char* safe_line1 = line1 ? line1 : "";
    const char* safe_line2 = line2 ? line2 : "";
    if (g_message_visible &&
        strcmp(g_last_message_line1, safe_line1) == 0 &&
        strcmp(g_last_message_line2, safe_line2) == 0) {
        return;
    }

    M5.Display.fillRect(0, 0, k_screen_width, k_footer_y, k_bg_color);
    M5.Display.setTextColor(k_text_color, k_bg_color);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(safe_line1, k_screen_width / 2, k_footer_y / 2 - 8);
    if (safe_line2[0] != '\0') {
        M5.Display.drawString(safe_line2, k_screen_width / 2, k_footer_y / 2 + 10);
    }
    M5.Display.setTextDatum(top_left);

    snprintf(g_last_message_line1, sizeof(g_last_message_line1), "%s", safe_line1);
    snprintf(g_last_message_line2, sizeof(g_last_message_line2), "%s", safe_line2);
    g_last_status_text[0] = '\0';
    g_message_visible = true;
}

static void clear_message_area_if_needed()
{
    if (!g_message_visible) {
        return;
    }

    M5.Display.fillRect(0, 0, k_screen_width, k_footer_y, k_bg_color);
    g_last_message_line1[0] = '\0';
    g_last_message_line2[0] = '\0';
    g_last_status_text[0] = '\0';
    g_message_visible = false;
}

static bool load_current_state_cache()
{
    const codex_pet::PetSpec& spec = current_spec();
    const codex_pet::Player& player = current_player();
    const codex_pet::StateInfo* state = codex_pet::stateInfo(spec, player.stateIndex);
    if (!state || state->frameCount == 0) {
        ESP_LOGE(TAG, "invalid state: pet=%s state=%u", spec.petId, static_cast<unsigned>(player.stateIndex));
        draw_message("invalid animation state");
        return false;
    }

    if (!g_sd_ready) {
        draw_message("SD card not mounted");
        return false;
    }

    if (g_state_cache.pet_index == g_current_pet_index &&
        g_state_cache.state_index == player.stateIndex &&
        g_state_cache.frame_count == state->frameCount) {
        return true;
    }

    if (g_state_cache.failed_pet_index == g_current_pet_index &&
        g_state_cache.failed_state_index == player.stateIndex) {
        return false;
    }

    if (spec.frameWidth != k_pet_width || spec.frameHeight != k_pet_height) {
        ESP_LOGE(TAG, "unsupported frame size: %ux%u", spec.frameWidth, spec.frameHeight);
        draw_message("unsupported frame size");
        return false;
    }

    if (!ensure_state_cache_capacity(state->frameCount)) {
        draw_message("state cache allocation failed");
        return false;
    }

    ESP_LOGI(
        TAG,
        "loading state cache: pet=%s state=%s frames=%u",
        spec.petId,
        state->name,
        static_cast<unsigned>(state->frameCount));

    for (uint8_t frame = 0; frame < state->frameCount; ++frame) {
        char sd_path[192] = {};
        if (!make_sd_png_frame_path(sd_path, sizeof(sd_path), spec, player.stateIndex, frame)) {
            return false;
        }
        uint16_t* dst = g_state_cache.pixels + static_cast<size_t>(frame) * k_pet_width * k_pet_height;
        if (!decode_png_to_cache(sd_path, dst)) {
            g_state_cache.pet_index = SIZE_MAX;
            g_state_cache.state_index = UINT8_MAX;
            g_state_cache.frame_count = 0;
            g_state_cache.failed_pet_index = g_current_pet_index;
            g_state_cache.failed_state_index = player.stateIndex;
            draw_message("PNG asset not found", sd_path);
            return false;
        }
    }

    g_state_cache.pet_index = g_current_pet_index;
    g_state_cache.state_index = player.stateIndex;
    g_state_cache.frame_count = state->frameCount;
    g_state_cache.failed_pet_index = SIZE_MAX;
    g_state_cache.failed_state_index = UINT8_MAX;
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
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawCenterString("<", left_width / 2, k_footer_y + 16);
    M5.Display.drawCenterString("*", left_width + center_width / 2, k_footer_y + 16);
    M5.Display.drawCenterString(">", left_width + center_width + right_width / 2, k_footer_y + 16);
    M5.Display.setTextDatum(top_left);
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

    if (!g_message_visible && strcmp(g_last_status_text, text) == 0) {
        return;
    }

    M5.Display.fillRect(0, 0, k_screen_width, 24, k_bg_color);
    M5.Display.setTextColor(k_text_color, k_bg_color);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_left);
    M5.Display.drawString(text, 8, 6);
    snprintf(g_last_status_text, sizeof(g_last_status_text), "%s", text);
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

    clear_message_area_if_needed();

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

    M5.begin();
    M5.Display.setBrightness(128);
    M5.Display.setRotation(1);

    mount_asset_storage();

    for (auto& pet : g_pets) {
        pet.player = codex_pet::makePlayer(*pet.spec, 0, now_ms());
    }

    create_ui();
    render_current_frame();
    current_player().dirty = false;

    xTaskCreate(animation_task, "pet_anim", 8192, nullptr, 5, nullptr);
}
