#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "driver/gpio.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_lcd_1_83.h"
#include "bsp_board_extra.h"
#include "lv_demos.h"
#include "esp_jpeg_dec.h"
#include "avi_player.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "main";

#define DISP_WIDTH 240
#define DISP_HEIGHT 240

static lv_obj_t *canvas = NULL;
static lv_color_t *canvas_buf[2] = {NULL};
static int current_buf_idx = 0;
static avi_player_handle_t avi_handle = NULL;
static volatile bool reload_requested = false;
static lv_obj_t *status_label = NULL;
static lv_obj_t *title_label = NULL;
static bool loop_playback = true;
static bool is_playing = false;
static volatile bool is_paused = false;
static volatile bool next_track_requested = false;

static jpeg_dec_handle_t jpeg_handle = NULL;

static char **avi_file_list = NULL;
static int avi_file_count = 0;

#define LVGL_PORT_INIT_CONFIG()   \
    {                             \
        .task_priority = 4,       \
        .task_stack = 10 * 1024,  \
        .task_affinity = -1,      \
        .task_max_sleep_ms = 500, \
        .timer_period_ms = 5,     \
    }

static esp_err_t get_avi_file_list(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Failed to open directory: %s", dir_path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".avi") == 0)) {
                count++;
            }
        }
    }

    if (count == 0) {
        closedir(dir);
        ESP_LOGW(TAG, "No AVI files found in directory %s", dir_path);
        return ESP_FAIL;
    }

    avi_file_list = (char **)malloc(sizeof(char *) * count);
    if (!avi_file_list) {
        closedir(dir);
        ESP_LOGE(TAG, "Failed to allocate memory for file list");
        return ESP_ERR_NO_MEM;
    }
    avi_file_count = count;
    count = 0;

    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".avi") == 0)) {
                size_t dir_len = strlen(dir_path);
                size_t file_len = strlen(entry->d_name);
                char *full_path = (char *)malloc(dir_len + file_len + 2);
                if (!full_path) {
                    ESP_LOGE(TAG, "Failed to allocate memory for file path");
                    for (int i = 0; i < count; i++) {
                        free(avi_file_list[i]);
                    }
                    free(avi_file_list);
                    avi_file_list = NULL;
                    avi_file_count = 0;
                    closedir(dir);
                    return ESP_ERR_NO_MEM;
                }

                if (dir_len > 0 && dir_path[dir_len - 1] == '/') {
                    sprintf(full_path, "%s%s", dir_path, entry->d_name);
                } else {
                    sprintf(full_path, "%s/%s", dir_path, entry->d_name);
                }

                avi_file_list[count++] = full_path;
            }
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Found %d AVI files in directory %s", avi_file_count, dir_path);
    for (int i = 0; i < avi_file_count; i++) {
        ESP_LOGI(TAG, "AVI file %d: %s", i + 1, avi_file_list[i]);
    }

    return ESP_OK;
}

static void screen_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Screen clicked: Toggle Pause");
        is_paused = !is_paused;
    }
}

static void init_canvas(void)
{
    if (canvas == NULL) {
        for (int i = 0; i < 2; i++) {
            canvas_buf[i] = (lv_color_t *)jpeg_calloc_align(DISP_WIDTH * DISP_HEIGHT * sizeof(lv_color_t), 16);
            if (!canvas_buf[i]) {
                ESP_LOGE("init_canvas", "Failed to allocate memory for canvas buffer %d", i);
                for (int j = 0; j < i; j++) {
                    if (canvas_buf[j]) {
                        jpeg_free_align(canvas_buf[j]);
                        canvas_buf[j] = NULL;
                    }
                }
                return;
            }
        }

        canvas = lv_canvas_create(lv_scr_act());
        lv_canvas_set_buffer(canvas, canvas_buf[0], DISP_WIDTH, DISP_HEIGHT, LV_COLOR_FORMAT_RGB565);
        lv_obj_center(canvas);

        lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(canvas, screen_touch_cb, LV_EVENT_CLICKED, NULL);
    }
}

static esp_err_t init_jpeg_decoder(void)
{
    if (jpeg_handle != NULL) {
        return ESP_OK;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_error_t err = jpeg_dec_open(&config, &jpeg_handle);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE("init_jpeg_decoder", "JPEG decoder initialization failed: %d", err);
        return ESP_FAIL;
    }

    return ESP_OK;
}



static void video_cb(frame_data_t *data, void *arg)
{
    while (is_paused) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!data || !data->data || data->data_bytes == 0)
        return;

    int next_buf_idx = (current_buf_idx + 1) % 2;

    if (init_jpeg_decoder() != ESP_OK) {
        return;
    }

    jpeg_dec_io_t io = {
        .inbuf = data->data,
        .inbuf_len = data->data_bytes,
        .outbuf = (uint8_t *)canvas_buf[next_buf_idx],
    };

    jpeg_dec_header_info_t header_info;
    jpeg_error_t err = jpeg_dec_parse_header(jpeg_handle, &io, &header_info);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE("video_cb", "JPEG header parsing failed: %d", err);
        return;
    }

    int outbuf_len = 0;
    err = jpeg_dec_get_outbuf_len(jpeg_handle, &outbuf_len);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE("video_cb", "Failed to get output buffer length: %d", err);
        return;
    }

    if (outbuf_len > DISP_WIDTH * DISP_HEIGHT * 2) {
        ESP_LOGE("video_cb", "Output buffer too small. Required %d bytes, available %d bytes",
                 outbuf_len, DISP_WIDTH * DISP_HEIGHT * 2);
        return;
    }

    err = jpeg_dec_process(jpeg_handle, &io);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE("video_cb", "JPEG decoding failed: %d", err);
        return;
    }

    bsp_display_lock(0);
    if (canvas == NULL) {
        init_canvas();
    }

    lv_canvas_set_buffer(canvas, canvas_buf[next_buf_idx], DISP_WIDTH, DISP_HEIGHT, LV_COLOR_FORMAT_RGB565);
    current_buf_idx = next_buf_idx;
    lv_obj_invalidate(canvas);

    bsp_display_unlock();
}

static void audio_cb(frame_data_t *data, void *arg)
{
    while (is_paused) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (data && data->type == FRAME_TYPE_AUDIO && data->data && data->data_bytes > 0) {
        size_t bytes_written = 0;
        esp_err_t err = bsp_extra_i2s_write(data->data, data->data_bytes, &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Audio write failed: %s", esp_err_to_name(err));
        } else if (bytes_written != data->data_bytes) {
            ESP_LOGW(TAG, "Incomplete audio data (wrote %d/%d bytes)", bytes_written, data->data_bytes);
        }
    }
}

static void audio_set_clock_callback(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg)
{
    if (rate == 0) {
        rate = CODEC_DEFAULT_SAMPLE_RATE;
        ESP_LOGW(TAG, "Using default sample rate: %u", rate);
    }
    if (bits_cfg == 0) {
        bits_cfg = CODEC_DEFAULT_BIT_WIDTH;
        ESP_LOGW(TAG, "Using default bit width: %u", bits_cfg);
    }

    ESP_LOGI(TAG, "Setting I2S clock: sample rate=%u, bit width=%u, channels=%u", rate, bits_cfg, ch);
    i2s_slot_mode_t slot_mode = (ch == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    esp_err_t err = bsp_extra_codec_set_fs(rate, bits_cfg, slot_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set codec parameters: %s", esp_err_to_name(err));
    }
}

static void avi_end_cb(void *arg)
{
    ESP_LOGI(TAG, "AVI playback finished");
    is_playing = false;
}

static void input_task(void *arg)
{
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);

    const TickType_t double_click_window = pdMS_TO_TICKS(300);
    uint32_t first_click_tick = 0;
    int pending_clicks = 0;
    
    while (1) {
        if (pending_clicks == 1 && (xTaskGetTickCount() - first_click_tick) > double_click_window) {
            ESP_LOGI(TAG, "Single click: Toggle Pause");
            is_paused = !is_paused;
            pending_clicks = 0;
        }

        if (gpio_get_level(GPIO_NUM_0) == 0) {
            uint32_t press_start = xTaskGetTickCount();
            bool long_press_handled = false;
            
            while (gpio_get_level(GPIO_NUM_0) == 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
                if (!long_press_handled && (xTaskGetTickCount() - press_start) > pdMS_TO_TICKS(1000)) {
                    // Long press detected
                    ESP_LOGI(TAG, "Long press: Reloading...");
                    is_paused = false; // Unpause if paused so the task can proceed to stop
                    pending_clicks = 0;
                    reload_requested = true;
                    if (avi_handle) {
                        avi_player_play_stop(avi_handle);
                    }
                    long_press_handled = true;
                }
            }
            
            if (!long_press_handled) {
                // Short press: defer action to distinguish single vs double click
                uint32_t now = xTaskGetTickCount();
                if (pending_clicks == 0) {
                    pending_clicks = 1;
                    first_click_tick = now;
                } else if ((now - first_click_tick) <= double_click_window) {
                    ESP_LOGI(TAG, "Double click: Next track");
                    pending_clicks = 0;
                    next_track_requested = true;
                    is_paused = false;
                    if (avi_handle) {
                        avi_player_play_stop(avi_handle);
                    }
                } else {
                    // Window expired but not yet processed; treat as new first click
                    pending_clicks = 1;
                    first_click_tick = now;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}

static lv_obj_t *vol_popup = NULL;
static lv_obj_t *vol_slider = NULL;
static bool was_paused_before_vol = false;

static void volume_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int vol = (int)lv_slider_get_value(slider);
    bsp_extra_codec_volume_set(vol, NULL);
}

static void volume_ok_cb(lv_event_t *e)
{
    if (vol_popup) {
        lv_obj_add_flag(vol_popup, LV_OBJ_FLAG_HIDDEN);
    }
    if (!was_paused_before_vol) {
        is_paused = false;
    }
}

static void volume_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        was_paused_before_vol = is_paused;
        is_paused = true;

        if (vol_popup == NULL) {
            vol_popup = lv_obj_create(lv_layer_top());
            lv_obj_set_size(vol_popup, 200, 150);
            lv_obj_center(vol_popup);
            
            lv_obj_t *label = lv_label_create(vol_popup);
            lv_label_set_text(label, "Volume");
            lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

            vol_slider = lv_slider_create(vol_popup);
            lv_obj_set_width(vol_slider, 160);
            lv_obj_align(vol_slider, LV_ALIGN_CENTER, 0, -10);
            lv_slider_set_range(vol_slider, 0, 100);
            lv_slider_set_value(vol_slider, bsp_extra_codec_volume_get(), LV_ANIM_OFF);
            lv_obj_add_event_cb(vol_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

            lv_obj_t *btn = lv_btn_create(vol_popup);
            lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_event_cb(btn, volume_ok_cb, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t *btn_label = lv_label_create(btn);
            lv_label_set_text(btn_label, "OK");
            lv_obj_center(btn_label);
        } else {
            lv_obj_clear_flag(vol_popup, LV_OBJ_FLAG_HIDDEN);
            lv_slider_set_value(vol_slider, bsp_extra_codec_volume_get(), LV_ANIM_OFF);
            lv_obj_move_foreground(vol_popup);
        }
    }
}

static void avi_play_task(void *arg)
{
    avi_player_config_t cfg = {
        .buffer_size = 1 * 1024 * 1024, // 1MB psram buffer (1-2 seconds of video)
        .video_cb = video_cb,
        .audio_cb = audio_cb,
        .audio_set_clock_cb = audio_set_clock_callback,
        .avi_play_end_cb = avi_end_cb,
        .priority = 7,
        .coreID = 1,
        .user_data = NULL,
        .stack_size = 12 * 1024,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
        .stack_in_psram = true,
#endif
    };

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
    init_canvas();
    bsp_display_unlock();

    ESP_ERROR_CHECK(avi_player_init(cfg, &avi_handle));

    bsp_display_lock(0);
    lv_obj_t *vol_btn = lv_btn_create(lv_layer_top());
    lv_obj_set_size(vol_btn, 40, 40);
    lv_obj_align(vol_btn, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_bg_color(vol_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(vol_btn, LV_OPA_50, 0);
    
    lv_obj_t *lbl = lv_label_create(vol_btn);
    lv_label_set_text(lbl, LV_SYMBOL_VOLUME_MAX);
    lv_obj_center(lbl);
    
    lv_obj_add_event_cb(vol_btn, volume_btn_cb, LV_EVENT_CLICKED, NULL);
    bsp_display_unlock();

    while (1) {
        if (reload_requested) {
            reload_requested = false;
            bsp_display_lock(0);
            if (canvas) {
                lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
                lv_obj_invalidate(canvas);
            }
            bsp_display_unlock();
            bsp_sdcard_unmount();
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        // Mount SD
        if (bsp_sdcard_mount() != ESP_OK) {
            bsp_display_lock(0);
            if (canvas) {
                lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
            }
            if (!status_label) {
                 status_label = lv_label_create(lv_scr_act());
                 lv_obj_set_width(status_label, DISP_WIDTH - 20);
                 lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
                 lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
                 lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
                 lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
            }
            lv_label_set_text(status_label, "Insert SD Card\nPress BOOT to reload");
            lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();
            
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Scan files
        esp_err_t scan_ret = get_avi_file_list("/sdcard/videos");
        if (scan_ret != ESP_OK || avi_file_count == 0) {
             scan_ret = get_avi_file_list("/sdcard/avi");
        }

        if (scan_ret != ESP_OK || avi_file_count == 0) {
            bsp_display_lock(0);
            if (canvas) {
                lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
            }
            if (!status_label) {
                 status_label = lv_label_create(lv_scr_act());
                 lv_obj_set_width(status_label, DISP_WIDTH - 20);
                 lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
                 lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
                 lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
                 lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
            }
            lv_label_set_text(status_label, "No AVI files found");
            lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();

            bsp_sdcard_unmount();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Hide status label
        bsp_display_lock(0);
        if (status_label) {
            lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        }
        bsp_display_unlock();

        loop_playback = true;
        int current_file_index = 0;

        bsp_display_lock(0);
        if (canvas) {
            lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
        }
        bsp_display_unlock();

        while (loop_playback && !reload_requested) {
            for (current_file_index = 0; current_file_index < avi_file_count && loop_playback && !reload_requested; current_file_index++) {
                const char *current_file = avi_file_list[current_file_index];
                ESP_LOGI(TAG, "Playing: %s", current_file);

                bsp_display_lock(0);
                if (!title_label) {
                     title_label = lv_label_create(lv_scr_act());
                     lv_obj_set_width(title_label, DISP_WIDTH - 10);
                     lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
                     lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 5);
                     lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
                     lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
                     lv_obj_set_style_bg_color(title_label, lv_color_black(), 0);
                     lv_obj_set_style_bg_opa(title_label, LV_OPA_50, 0);
                }
                
                const char *fname = strrchr(current_file, '/');
                if (fname) fname++; else fname = current_file;
                
                lv_label_set_text(title_label, fname);
                lv_obj_clear_flag(title_label, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(title_label);
                bsp_display_unlock();

                uint32_t play_start_time = xTaskGetTickCount();
                bool title_hidden = false;
                
                is_playing = true;
                next_track_requested = false;
                if (avi_player_play_from_file(avi_handle, current_file) != ESP_OK) {
                    FILE *f = fopen(current_file, "r");
                    if (f) {
                        fclose(f);
                    } else {
                        ESP_LOGW(TAG, "File access failed, SD card removed?");
                        loop_playback = false;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }

                while (is_playing && loop_playback && !reload_requested) {
                    if (!title_hidden && (xTaskGetTickCount() - play_start_time > pdMS_TO_TICKS(2000))) {
                        bsp_display_lock(0);
                        if (title_label) lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);
                        bsp_display_unlock();
                        title_hidden = true;
                    }

                    if (next_track_requested) {
                        next_track_requested = false;
                        is_paused = false;
                        avi_player_play_stop(avi_handle);
                        is_playing = false;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(30));
                }
            }
            if (!loop_playback || reload_requested) break;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // Cleanup file list
        if (avi_file_list) {
            for (int i = 0; i < avi_file_count; i++) {
                free(avi_file_list[i]);
            }
            free(avi_file_list);
            avi_file_list = NULL;
            avi_file_count = 0;
        }

        bsp_sdcard_unmount();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_extra_codec_init());
    bsp_extra_codec_volume_set(80, NULL);
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = LVGL_PORT_INIT_CONFIG(),
        .buffer_size = DISP_WIDTH * DISP_HEIGHT / 2, // Partial double buffer
        .double_buffer = 1,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
        }
    };

    bsp_display_start_with_config(&cfg);
    
    bsp_display_lock(0);
    // bsp_display_rotate(disp, LV_DISPLAY_ROTATION_270); // Rotated in video file
    bsp_display_unlock();

    bsp_display_backlight_on();

    xTaskCreatePinnedToCore(avi_play_task, "avi_play_task", 12288, NULL, 7, NULL, 0);
    xTaskCreatePinnedToCore(input_task, "input_task", 4096, NULL, 5, NULL, 0);
}