#include "jammer_app.h"
#include <furi_hal_region.h>
#include <furi.h>
#include <gui/gui.h>
#include <subghz/devices/devices.h>
#include <furi_hal.h>
#include <lib/subghz/subghz_tx_rx_worker.h>
#include "helpers/radio_device_loader.h"
#include <furi_hal_random.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define TAG "JammerApp"
#define SUBGHZ_FREQUENCY_MIN 300000000
#define SUBGHZ_FREQUENCY_MAX 928000000
#define MESSAGE_MAX_LEN      1024

// ---- Region unlock ----

static FuriHalRegion unlockedRegion = {
    .country_code = "FTW",
    .bands_count = 3,
    .bands = {
        {.start = 299999755, .end = 348000000, .power_limit = 20, .duty_cycle = 50},
        {.start = 386999938, .end = 464000000, .power_limit = 20, .duty_cycle = 50},
        {.start = 778999847, .end = 928000000, .power_limit = 20, .duty_cycle = 50},
    },
};

// ---- Valid frequency bands ----

typedef struct { uint32_t min; uint32_t max; } FrequencyBand;

static const FrequencyBand valid_bands[] = {
    {300000000, 348000000},
    {387000000, 464000000},
    {779000000, 928000000},
};
#define NUM_BANDS (sizeof(valid_bands) / sizeof(valid_bands[0]))

// ---- String tables ----

static const char* mode_names[JammerModeCount] = {
    "OOK 650kHz",
    "2FSK 2.38kHz",
    "2FSK 47.6kHz",
    "MSK 99.97Kb/s",
    "GFSK 9.99Kb/s",
    "Bruteforce 0xFF",
    "Sine Wave",
    "Square Wave",
    "Sawtooth Wave",
    "White Noise",
    "Triangle Wave",
    "Chirp Signal",
    "Gaussian Noise",
    "Burst Mode",
    "Raw Noise",
};

static const char* menu_items[MENU_ITEMS_COUNT] = {
    "Jamming Mode",
    "Presets",
    "Sweep",
    "Raw Noise",
    "Back",
};

static const char* save_names[SAVE_NAMES_COUNT] = {
    "Car Keys",
    "Car Keys EU",
    "Car Keys US",
    "Garage",
    "Gate",
    "Alarm",
    "Door Bell",
    "Remote",
    "IoT 868",
    "IoT 915",
    "Key Fob",
    "Weather Stn",
    "Baby Monitor",
    "Tire Sensor",
    "Custom 1",
    "Custom 2",
    "Custom 3",
};

static const uint32_t sweep_steps[SWEEP_STEP_COUNT]       = {10000, 50000, 100000, 500000};
static const char*    sweep_step_names[SWEEP_STEP_COUNT]  = {"10 kHz", "50 kHz", "100 kHz", "500 kHz"};
static const uint32_t sweep_speeds[SWEEP_SPEED_COUNT]     = {50, 150, 500};
static const char*    sweep_speed_names[SWEEP_SPEED_COUNT] = {"Fast", "Medium", "Slow"};

// ---- Default presets ----

static void init_default_presets(JammerApp* app) {
    static const Preset defaults[] = {
        {"Car Keys",    434000000, JammerModeOok650Async},
        {"Car Keys US", 315000000, JammerModeOok650Async},
        {"Garage",      390000000, JammerModeOok650Async},
        {"Gate",        433920000, JammerModeOok650Async},
        {"ISM 868",     868350000, JammerModeOok650Async},
        {"ISM 915",     915000000, JammerModeOok650Async},
    };
    app->preset_count = 6;
    for(size_t i = 0; i < 6; i++) app->presets[i] = defaults[i];
}

// ---- Forward declarations ----

static void jammer_stop_tx(JammerApp* app);
static void jammer_start_tx(JammerApp* app);
static bool jammer_init_radio(JammerApp* app);

// ---- Frequency helpers ----

static bool is_frequency_valid(uint32_t freq) {
    for(size_t i = 0; i < NUM_BANDS; i++)
        if(freq >= valid_bands[i].min && freq <= valid_bands[i].max) return true;
    return false;
}

static uint32_t clamp_to_valid(uint32_t freq, bool up) {
    if(is_frequency_valid(freq)) return freq;
    if(up) {
        for(size_t i = 0; i < NUM_BANDS; i++)
            if(freq < valid_bands[i].min) return valid_bands[i].min;
        return valid_bands[0].min;
    } else {
        for(int i = (int)NUM_BANDS - 1; i >= 0; i--)
            if(freq > valid_bands[i].max) return valid_bands[i].max;
        return valid_bands[NUM_BANDS - 1].max;
    }
}

static void adjust_frequency(JammerApp* app, bool up) {
    uint32_t step;
    switch(app->cursor_position) {
        case 0: step = 100000000; break;
        case 1: step = 10000000;  break;
        case 2: step = 1000000;   break;
        case 3: step = 100000;    break;
        case 4: step = 10000;     break;
        default: return;
    }
    uint32_t freq = app->frequency;
    if(up)
        freq = (freq + step > SUBGHZ_FREQUENCY_MAX) ? SUBGHZ_FREQUENCY_MIN : freq + step;
    else
        freq = (freq < SUBGHZ_FREQUENCY_MIN + step) ? SUBGHZ_FREQUENCY_MAX : freq - step;
    freq = clamp_to_valid(freq, up);
    app->frequency = freq;
    if(app->jam_status == JamStatusRunning && app->subghz_txrx && app->device) {
        subghz_tx_rx_worker_stop(app->subghz_txrx);
        subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency);
    }
}

// ---- Draw helpers ----

// Shared list-item row used by menu, mode, presets, save-name screens
static void draw_list_item(Canvas* canvas, int item_y, bool selected, const char* label) {
    if(selected) {
        canvas_draw_box(canvas, 0, item_y - 1, 128, 10);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str_aligned(canvas, 64, item_y, AlignCenter, AlignTop, label);
    canvas_set_color(canvas, ColorBlack);
}

// ---- Screen draw functions ----

static void draw_jammer_screen(Canvas* canvas, JammerApp* app) {
    canvas_clear(canvas);

    // Outer border frame
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    // Header bar
    canvas_draw_box(canvas, 0, 0, 128, 11);
    canvas_set_color(canvas, ColorWhite);

    // Left antenna (T shape)
    canvas_draw_line(canvas, 6, 1, 6, 8);
    canvas_draw_line(canvas, 3, 1, 9, 1);
    // Right antenna
    canvas_draw_line(canvas, 121, 1, 121, 8);
    canvas_draw_line(canvas, 118, 1, 124, 1);

    // Title
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "RF JAMMER");

    // Header animation — changes with jam state
    uint8_t f = app->anim_frame;
    switch(app->jam_status) {

        case JamStatusRunning:
            // 3 dot-pairs scanning OUTWARD from antennas → title (active transmission)
            for(int i = 0; i < 3; i++) {
                int px = 10 + (int)(((f + (uint8_t)(i * 3)) % 8) * 3);
                if(px >= 10 && px <= 33) {
                    canvas_draw_dot(canvas, px, 4);
                    canvas_draw_dot(canvas, px, 6);
                }
            }
            for(int i = 0; i < 3; i++) {
                int px = 117 - (int)(((f + (uint8_t)(i * 3)) % 8) * 3);
                if(px >= 94 && px <= 117) {
                    canvas_draw_dot(canvas, px, 4);
                    canvas_draw_dot(canvas, px, 6);
                }
            }
            break;

        case JamStatusPaused:
            // 2 dot-pairs scanning INWARD back toward antennas (retreating signal)
            for(int i = 0; i < 2; i++) {
                int px = 33 - (int)(((f + (uint8_t)(i * 4)) % 8) * 3);
                if(px >= 10 && px <= 33) {
                    canvas_draw_dot(canvas, px, 4);
                    canvas_draw_dot(canvas, px, 6);
                }
            }
            for(int i = 0; i < 2; i++) {
                int px = 94 + (int)(((f + (uint8_t)(i * 4)) % 8) * 3);
                if(px >= 94 && px <= 117) {
                    canvas_draw_dot(canvas, px, 4);
                    canvas_draw_dot(canvas, px, 6);
                }
            }
            break;

        case JamStatusStopped:
        default:
            // Single dot near each antenna — slow idle ping (on 2 of every 8 frames)
            if(f < 2) {
                canvas_draw_dot(canvas, 10, 5);
                canvas_draw_dot(canvas, 117, 5);
            }
            break;
    }
    canvas_set_color(canvas, ColorBlack);

    // Signal arcs flanking the frequency (partial circles clipped by screen edge)
    canvas_draw_circle(canvas, 0,   25,  6);
    canvas_draw_circle(canvas, 0,   25, 11);
    canvas_draw_circle(canvas, 0,   25, 16);
    canvas_draw_circle(canvas, 127, 25,  6);
    canvas_draw_circle(canvas, 127, 25, 11);
    canvas_draw_circle(canvas, 127, 25, 16);

    // Frequency display
    char freq_str[8];
    snprintf(freq_str, sizeof(freq_str), "%03lu.%02lu",
        (unsigned long)(app->frequency / 1000000),
        (unsigned long)((app->frequency % 1000000) / 10000));

    int total_w = 5 * 12 + 6;
    int x = (128 - total_w) / 2;
    int y_freq = 30;
    int digit_idx = 0;

    for(size_t i = 0; i < strlen(freq_str); i++) {
        char ch[2] = {freq_str[i], '\0'};
        if(freq_str[i] == '.') {
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, x, y_freq, ch);
            x += 6;
        } else {
            canvas_set_font(canvas, FontBigNumbers);
            canvas_draw_str(canvas, x, y_freq, ch);
            if(digit_idx == (int)app->cursor_position)
                canvas_draw_line(canvas, x, y_freq + 2, x + 11, y_freq + 2);
            x += 12;
            digit_idx++;
        }
    }
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x + 2, y_freq, "MHz");

    // Separator
    canvas_draw_line(canvas, 18, 33, 109, 33);

    // Mode name
    canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignTop, mode_names[app->jamming_mode]);

    // Status badge
    const char* status_str;
    switch(app->jam_status) {
        case JamStatusRunning: status_str = app->sweep_enabled ? "~~ SWEEPING ~~" : ">> RUNNING <<"; break;
        case JamStatusPaused:  status_str = "PAUSED";        break;
        default:               status_str = "STOPPED";       break;
    }
    if(app->jam_status == JamStatusRunning) {
        canvas_draw_box(canvas, 2, 43, 124, 11);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignTop, status_str);
        canvas_set_color(canvas, ColorBlack);
    } else if(app->jam_status == JamStatusPaused) {
        canvas_draw_frame(canvas, 2, 43, 124, 11);
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignTop, status_str);
    } else {
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignTop, status_str);
    }

    // Hints
    canvas_set_font(canvas, FontSecondary);
    switch(app->jam_status) {
        case JamStatusRunning:
            canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignTop, "OK:Pause  Back:Menu");
            break;
        case JamStatusPaused:
            canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignTop, "OK:Resume  Back:Menu");
            break;
        default:
            canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignTop, "OK:Start  Back:Menu");
            break;
    }
}

static void draw_menu_screen(Canvas* canvas, JammerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Menu");
    canvas_draw_line(canvas, 0, 13, 128, 13);

    // Show 4 items at a time so the hint at y=56 is never overlapped
    int sel   = (int)app->menu_selection;
    int start = sel - 1;
    if(start < 0) start = 0;
    if(start > MENU_ITEMS_COUNT - 4) start = MENU_ITEMS_COUNT - 4;
    if(start < 0) start = 0;

    for(int i = 0; i < 4; i++) {
        int idx = start + i;
        if(idx >= MENU_ITEMS_COUNT) break;
        draw_list_item(canvas, 16 + i * 10, idx == sel, menu_items[idx]);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignTop, "OK:Select  Back:Return");
}

static void draw_mode_screen(Canvas* canvas, JammerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Jamming Mode");
    canvas_draw_line(canvas, 0, 13, 128, 13);

    int sel   = (int)app->mode_selection;
    int start = sel - MODE_LIST_VISIBLE / 2;
    if(start < 0) start = 0;
    if(start > (int)JammerModeCount - MODE_LIST_VISIBLE)
        start = (int)JammerModeCount - MODE_LIST_VISIBLE;

    for(int i = 0; i < MODE_LIST_VISIBLE; i++) {
        int idx = start + i;
        if(idx < 0 || idx >= (int)JammerModeCount) continue;
        draw_list_item(canvas, 16 + i * 10, idx == sel, mode_names[idx]);
    }
}

static void draw_presets_screen(Canvas* canvas, JammerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Presets");
    canvas_draw_line(canvas, 0, 13, 128, 13);

    int total = (int)app->preset_count + 1; // index 0 = "Save Current"
    int sel   = (int)app->preset_selection;
    int start = sel - PRESETS_LIST_VISIBLE / 2;
    if(start < 0) start = 0;
    if(start > total - PRESETS_LIST_VISIBLE) start = total - PRESETS_LIST_VISIBLE;
    if(start < 0) start = 0;

    for(int i = 0; i < PRESETS_LIST_VISIBLE; i++) {
        int idx = start + i;
        if(idx >= total) break;
        int item_y = 16 + i * 10;

        if(idx == 0) {
            draw_list_item(canvas, item_y, idx == sel, "+ Save Current");
        } else {
            Preset* p = &app->presets[idx - 1];
            char line[22];
            snprintf(line, sizeof(line), "%s %03lu.%02lu", p->name,
                (unsigned long)(p->frequency / 1000000),
                (unsigned long)((p->frequency % 1000000) / 10000));
            draw_list_item(canvas, item_y, idx == sel, line);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignTop, "OK:Load  [OK]:Del  Bk:Menu");
}

static void draw_save_name_screen(Canvas* canvas, JammerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Save As");
    canvas_draw_line(canvas, 0, 13, 128, 13);

    int sel   = (int)app->save_name_selection;
    int start = sel - MODE_LIST_VISIBLE / 2;
    if(start < 0) start = 0;
    if(start > SAVE_NAMES_COUNT - MODE_LIST_VISIBLE)
        start = SAVE_NAMES_COUNT - MODE_LIST_VISIBLE;

    for(int i = 0; i < MODE_LIST_VISIBLE; i++) {
        int idx = start + i;
        if(idx >= SAVE_NAMES_COUNT) break;
        draw_list_item(canvas, 16 + i * 10, idx == sel, save_names[idx]);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignTop, "OK:Save  Back:Cancel");
}

static void draw_sweep_screen(Canvas* canvas, JammerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Sweep Mode");
    canvas_draw_line(canvas, 0, 12, 128, 12);

    canvas_set_font(canvas, FontSecondary);

    // From row (static — shows the current jammer frequency)
    char from_buf[24];
    snprintf(from_buf, sizeof(from_buf), "From: %03lu.%02lu MHz",
        (unsigned long)(app->sweep_freq_a / 1000000),
        (unsigned long)((app->sweep_freq_a % 1000000) / 10000));
    canvas_draw_str_aligned(canvas, 64, 15, AlignCenter, AlignTop, from_buf);

    // To row (selectable when sweep_row == 0)
    char to_buf[24];
    snprintf(to_buf, sizeof(to_buf), "To:   %03lu.%02lu MHz",
        (unsigned long)(app->sweep_freq_b / 1000000),
        (unsigned long)((app->sweep_freq_b % 1000000) / 10000));
    draw_list_item(canvas, 26, app->sweep_row == 0, to_buf);

    // Step row (selectable when sweep_row == 1)
    char step_buf[20];
    snprintf(step_buf, sizeof(step_buf), "Step: %s", sweep_step_names[app->sweep_step_idx]);
    draw_list_item(canvas, 36, app->sweep_row == 1, step_buf);

    // Speed row (selectable when sweep_row == 2)
    char speed_buf[20];
    snprintf(speed_buf, sizeof(speed_buf), "Spd:  %s", sweep_speed_names[app->sweep_speed_idx]);
    draw_list_item(canvas, 46, app->sweep_row == 2, speed_buf);

    // Hint
    canvas_set_font(canvas, FontSecondary);
    if(app->sweep_row < 2) {
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignTop, "OK:Next  Back:Menu");
    } else {
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignTop, "OK:Start  Back:Menu");
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    JammerApp* app = (JammerApp*)context;
    switch(app->screen) {
        case AppScreenJammer:     draw_jammer_screen(canvas, app);    break;
        case AppScreenMenu:       draw_menu_screen(canvas, app);      break;
        case AppScreenModeSelect: draw_mode_screen(canvas, app);      break;
        case AppScreenPresets:    draw_presets_screen(canvas, app);   break;
        case AppScreenSaveName:   draw_save_name_screen(canvas, app); break;
        case AppScreenSweep:      draw_sweep_screen(canvas, app);     break;
    }
}

static void input_callback(InputEvent* event, void* context) {
    JammerApp* app = (JammerApp*)context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

// ---- Radio ----

static bool jammer_init_radio(JammerApp* app) {
    app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
    if(!app->device) {
        FURI_LOG_W(TAG, "External CC1101 not found, trying internal.");
        app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeInternal);
        if(!app->device) { FURI_LOG_E(TAG, "No radio device found."); return false; }
    }
    subghz_devices_reset(app->device);
    subghz_devices_idle(app->device);
    FURI_LOG_I(TAG, "Radio initialized: %s", app->device->name);
    return true;
}

static void jammer_apply_preset(JammerApp* app) {
    FuriHalSubGhzPreset preset;
    switch(app->jamming_mode) {
        case JammerMode2FSKDev238Async: preset = FuriHalSubGhzPreset2FSKDev238Async; break;
        case JammerMode2FSKDev476Async: preset = FuriHalSubGhzPreset2FSKDev476Async; break;
        case JammerModeMSK99_97KbAsync: preset = FuriHalSubGhzPresetMSK99_97KbAsync; break;
        case JammerModeGFSK9_99KbAsync: preset = FuriHalSubGhzPresetGFSK9_99KbAsync; break;
        default:                        preset = FuriHalSubGhzPresetOok650Async;     break;
    }
    subghz_devices_load_preset(app->device, preset, NULL);
}

// ---- TX thread ----

static int32_t tx_thread_func(void* context) {
    JammerApp* app = (JammerApp*)context;
    uint8_t jam_data[MESSAGE_MAX_LEN];

    switch(app->jamming_mode) {
        case JammerModeOok650Async:
        case JammerModeBruteforce:
            memset(jam_data, 0xFF, sizeof(jam_data));
            break;
        case JammerMode2FSKDev238Async:
        case JammerMode2FSKDev476Async:
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (i % 2 == 0) ? 0xAA : 0x55;
            break;
        case JammerModeMSK99_97KbAsync:
        case JammerModeGFSK9_99KbAsync:
        case JammerModeWhiteNoise:
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (uint8_t)(rand() % 256);
            break;
        case JammerModeSineWave:
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (uint8_t)(127.0f * sinf(2.0f * (float)M_PI * i / sizeof(jam_data)) + 128.0f);
            break;
        case JammerModeSquareWave:
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (i % 2 == 0) ? 0xFF : 0x00;
            break;
        case JammerModeSawtoothWave:
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (uint8_t)(255 * i / sizeof(jam_data));
            break;
        case JammerModeTriangleWave: {
            size_t half = sizeof(jam_data) / 2;
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (i < half)
                    ? (uint8_t)(255 * i / half)
                    : (uint8_t)(255 - 255 * (i - half) / half);
            break;
        }
        case JammerModeChirp:
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (uint8_t)(127.0f * sinf(2.0f * (float)M_PI * i * (1.0f + (float)i / sizeof(jam_data))) + 128.0f);
            break;
        case JammerModeGaussianNoise:
            for(size_t i = 0; i < sizeof(jam_data); i++) {
                float u1 = ((float)(rand() + 1)) / ((float)RAND_MAX + 2.0f);
                float u2 = (float)rand() / (float)RAND_MAX;
                float g  = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
                jam_data[i] = (uint8_t)(127.0f * g + 128.0f);
            }
            break;
        case JammerModeBurst:
            for(size_t i = 0; i < sizeof(jam_data); i++)
                jam_data[i] = (i % 10 == 0) ? 0xFF : 0x00;
            break;
        case JammerModeRawNoise:
            furi_hal_random_fill_buf(jam_data, sizeof(jam_data));
            break;
        default:
            memset(jam_data, 0xFF, sizeof(jam_data));
            break;
    }

    while(app->jam_status == JamStatusRunning && app->subghz_txrx) {
        if(!subghz_tx_rx_worker_write(app->subghz_txrx, jam_data, sizeof(jam_data)))
            furi_delay_ms(20);
        furi_delay_ms(10);
    }
    return 0;
}

static void jammer_start_tx(JammerApp* app) {
    subghz_devices_begin(app->device);
    subghz_devices_reset(app->device);
    subghz_devices_idle(app->device);
    jammer_apply_preset(app);

    if(!subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency)) {
        FURI_LOG_E(TAG, "Failed to start TX worker.");
        app->jam_status = JamStatusStopped;
        return;
    }
    app->tx_thread = furi_thread_alloc();
    furi_thread_set_name(app->tx_thread, "Jammer TX");
    furi_thread_set_stack_size(app->tx_thread, 2048);
    furi_thread_set_context(app->tx_thread, app);
    furi_thread_set_callback(app->tx_thread, tx_thread_func);
    furi_thread_start(app->tx_thread);
    FURI_LOG_I(TAG, "TX: %s @ %luHz", mode_names[app->jamming_mode], (unsigned long)app->frequency);
}

static void jammer_stop_tx(JammerApp* app) {
    if(app->subghz_txrx && subghz_tx_rx_worker_is_running(app->subghz_txrx))
        subghz_tx_rx_worker_stop(app->subghz_txrx);
    if(app->tx_thread) {
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
    }
}

// ---- Shared helper: apply mode + optional restart ----

static void apply_mode(JammerApp* app, JammerMode mode) {
    bool was_running = (app->jam_status == JamStatusRunning);
    if(was_running) { app->jam_status = JamStatusStopped; jammer_stop_tx(app); }
    app->jamming_mode  = mode;
    app->sweep_enabled = false;
    if(was_running) { app->jam_status = JamStatusRunning; jammer_start_tx(app); }
}

// ---- Input handlers ----

static void handle_jammer_input(JammerApp* app, InputEvent* ev) {
    if(ev->type == InputTypeShort) {
        switch(ev->key) {
            case InputKeyOk:
                if(app->jam_status == JamStatusRunning) {
                    app->jam_status = JamStatusPaused;
                    jammer_stop_tx(app);
                } else {
                    app->jam_status = JamStatusRunning;
                    jammer_start_tx(app);
                }
                break;
            case InputKeyBack:
                app->menu_selection = 0;
                app->screen = AppScreenMenu;
                break;
            case InputKeyLeft:
                if(app->cursor_position > 0) app->cursor_position--;
                break;
            case InputKeyRight:
                if(app->cursor_position < 4) app->cursor_position++;
                break;
            case InputKeyUp:   adjust_frequency(app, true);  break;
            case InputKeyDown: adjust_frequency(app, false); break;
            default: break;
        }
    } else if(ev->type == InputTypeLong) {
        switch(ev->key) {
            case InputKeyOk:
                if(app->jam_status != JamStatusStopped) {
                    app->jam_status    = JamStatusStopped;
                    app->sweep_enabled = false;
                    jammer_stop_tx(app);
                }
                break;
            case InputKeyBack:
                if(app->jam_status != JamStatusStopped) {
                    app->jam_status    = JamStatusStopped;
                    app->sweep_enabled = false;
                    jammer_stop_tx(app);
                }
                app->app_running = false;
                break;
            default: break;
        }
    } else if(ev->type == InputTypeRepeat) {
        if(ev->key == InputKeyUp)   adjust_frequency(app, true);
        if(ev->key == InputKeyDown) adjust_frequency(app, false);
    }
}

static void handle_menu_input(JammerApp* app, InputEvent* ev) {
    if(ev->type == InputTypeShort || ev->type == InputTypeRepeat) {
        switch(ev->key) {
            case InputKeyUp:
                if(app->menu_selection > 0) app->menu_selection--;
                break;
            case InputKeyDown:
                if(app->menu_selection < MENU_ITEMS_COUNT - 1) app->menu_selection++;
                break;
            case InputKeyOk:
                switch(app->menu_selection) {
                    case 0: // Jamming Mode
                        app->mode_selection = (uint8_t)app->jamming_mode;
                        app->screen = AppScreenModeSelect;
                        break;
                    case 1: // Presets
                        app->preset_selection = 0;
                        app->screen = AppScreenPresets;
                        break;
                    case 2: // Sweep
                        app->sweep_freq_a = app->frequency;
                        if(app->sweep_freq_b <= app->sweep_freq_a)
                            app->sweep_freq_b = app->sweep_freq_a + 80000;
                        app->sweep_row = 0;
                        app->screen = AppScreenSweep;
                        break;
                    case 3: // Raw Noise shortcut
                        apply_mode(app, JammerModeRawNoise);
                        app->screen = AppScreenJammer;
                        break;
                    default: // Back
                        app->screen = AppScreenJammer;
                        break;
                }
                break;
            case InputKeyBack:
                app->screen = AppScreenJammer;
                break;
            default: break;
        }
    }
}

static void handle_mode_input(JammerApp* app, InputEvent* ev) {
    if(ev->type == InputTypeShort || ev->type == InputTypeRepeat) {
        switch(ev->key) {
            case InputKeyUp:
                if(app->mode_selection > 0) app->mode_selection--;
                break;
            case InputKeyDown:
                if(app->mode_selection < (uint8_t)(JammerModeCount - 1)) app->mode_selection++;
                break;
            case InputKeyOk:
                apply_mode(app, (JammerMode)app->mode_selection);
                app->screen = AppScreenJammer;
                break;
            case InputKeyBack:
                app->screen = AppScreenMenu;
                break;
            default: break;
        }
    }
}

static void handle_presets_input(JammerApp* app, InputEvent* ev) {
    int total = (int)app->preset_count + 1;

    if(ev->type == InputTypeShort || ev->type == InputTypeRepeat) {
        switch(ev->key) {
            case InputKeyUp:
                if(app->preset_selection > 0) app->preset_selection--;
                break;
            case InputKeyDown:
                if((int)app->preset_selection < total - 1) app->preset_selection++;
                break;
            case InputKeyOk:
                if(app->preset_selection == 0) {
                    app->save_name_selection = 0;
                    app->screen = AppScreenSaveName;
                } else {
                    Preset* p = &app->presets[app->preset_selection - 1];
                    bool was_running = (app->jam_status == JamStatusRunning);
                    if(was_running) { app->jam_status = JamStatusStopped; jammer_stop_tx(app); }
                    app->frequency    = p->frequency;
                    app->jamming_mode = p->mode;
                    if(was_running) { app->jam_status = JamStatusRunning; jammer_start_tx(app); }
                    app->screen = AppScreenJammer;
                }
                break;
            case InputKeyBack:
                app->screen = AppScreenMenu;
                break;
            default: break;
        }
    } else if(ev->type == InputTypeLong && ev->key == InputKeyOk) {
        // Long OK: delete preset (not the "Save Current" entry)
        if(app->preset_selection > 0 && app->preset_count > 0) {
            uint8_t del = app->preset_selection - 1;
            for(uint8_t i = del; i < app->preset_count - 1; i++)
                app->presets[i] = app->presets[i + 1];
            app->preset_count--;
            int new_total = (int)app->preset_count + 1;
            if((int)app->preset_selection >= new_total)
                app->preset_selection = (uint8_t)(new_total - 1);
        }
    }
}

static void handle_save_name_input(JammerApp* app, InputEvent* ev) {
    if(ev->type == InputTypeShort || ev->type == InputTypeRepeat) {
        switch(ev->key) {
            case InputKeyUp:
                if(app->save_name_selection > 0) app->save_name_selection--;
                break;
            case InputKeyDown:
                if(app->save_name_selection < SAVE_NAMES_COUNT - 1) app->save_name_selection++;
                break;
            case InputKeyOk:
                if(app->preset_count < MAX_PRESETS) {
                    Preset p;
                    strncpy(p.name, save_names[app->save_name_selection], PRESET_NAME_LEN - 1);
                    p.name[PRESET_NAME_LEN - 1] = '\0';
                    p.frequency = app->frequency;
                    p.mode      = app->jamming_mode;
                    app->presets[app->preset_count] = p;
                    app->preset_selection = (uint8_t)(app->preset_count + 1); // highlight new entry
                    app->preset_count++;
                }
                app->screen = AppScreenPresets;
                break;
            case InputKeyBack:
                app->screen = AppScreenPresets;
                break;
            default: break;
        }
    }
}

static void handle_sweep_input(JammerApp* app, InputEvent* ev) {
    if(ev->type == InputTypeShort || ev->type == InputTypeRepeat) {
        switch(ev->key) {
            case InputKeyUp:
                if(app->sweep_row == 0) {
                    uint32_t nf = app->sweep_freq_b + 10000;
                    if(nf > SUBGHZ_FREQUENCY_MAX) nf = SUBGHZ_FREQUENCY_MAX;
                    app->sweep_freq_b = clamp_to_valid(nf, true);
                } else if(app->sweep_row == 1) {
                    if(app->sweep_step_idx > 0) app->sweep_step_idx--;
                } else {
                    if(app->sweep_speed_idx > 0) app->sweep_speed_idx--;
                }
                break;
            case InputKeyDown:
                if(app->sweep_row == 0) {
                    uint32_t nf = (app->sweep_freq_b > SUBGHZ_FREQUENCY_MIN + 10000)
                        ? app->sweep_freq_b - 10000
                        : SUBGHZ_FREQUENCY_MIN;
                    app->sweep_freq_b = clamp_to_valid(nf, false);
                } else if(app->sweep_row == 1) {
                    if(app->sweep_step_idx < SWEEP_STEP_COUNT - 1) app->sweep_step_idx++;
                } else {
                    if(app->sweep_speed_idx < SWEEP_SPEED_COUNT - 1) app->sweep_speed_idx++;
                }
                break;
            case InputKeyRight:
                if(app->sweep_row == 0) {
                    uint32_t nf = app->sweep_freq_b + 100000;
                    if(nf > SUBGHZ_FREQUENCY_MAX) nf = SUBGHZ_FREQUENCY_MAX;
                    app->sweep_freq_b = clamp_to_valid(nf, true);
                }
                break;
            case InputKeyLeft:
                if(app->sweep_row == 0) {
                    uint32_t nf = (app->sweep_freq_b > SUBGHZ_FREQUENCY_MIN + 100000)
                        ? app->sweep_freq_b - 100000
                        : SUBGHZ_FREQUENCY_MIN;
                    app->sweep_freq_b = clamp_to_valid(nf, false);
                }
                break;
            case InputKeyOk:
                if(app->sweep_row < 2) {
                    app->sweep_row++;
                } else {
                    // Start sweep
                    app->sweep_enabled  = true;
                    app->sweep_going_up = true;
                    app->sweep_last_hop = furi_get_tick();
                    if(app->jam_status != JamStatusRunning) {
                        app->jam_status = JamStatusRunning;
                        jammer_start_tx(app);
                    }
                    app->screen = AppScreenJammer;
                }
                break;
            case InputKeyBack:
                app->sweep_row = 0;
                app->screen    = AppScreenMenu;
                break;
            default: break;
        }
    }
}

// ---- Animation timer ----

static void timer_callback(void* ctx) {
    JammerApp* app = (JammerApp*)ctx;
    app->anim_frame = (app->anim_frame + 1) % 8;

    // Dedicated LED tick — independent of animation frame
    // Timer fires every 50 ms, so led_tick cycles 0-9 over 500 ms
    static uint8_t led_tick = 0;
    if(++led_tick >= 10) led_tick = 0;
    bool fast = (led_tick % 2 == 0);        // 100 ms period = 10 Hz
    bool slow = ((led_tick / 5) % 2 == 0);  // 500 ms period =  2 Hz

    switch(app->jam_status) {
        case JamStatusRunning:
            if(app->sweep_enabled) {
                // Blue — sweeping
                furi_hal_light_set(LightRed,   0);
                furi_hal_light_set(LightGreen, 0);
                furi_hal_light_set(LightBlue,  fast ? 255 : 0);
            } else {
                // Green — running
                furi_hal_light_set(LightRed,   0);
                furi_hal_light_set(LightGreen, fast ? 255 : 0);
                furi_hal_light_set(LightBlue,  0);
            }
            break;
        case JamStatusPaused:
            // Orange — paused, slow blink
            furi_hal_light_set(LightRed,   slow ? 255 : 0);
            furi_hal_light_set(LightGreen, slow ?  60 : 0);
            furi_hal_light_set(LightBlue,  0);
            break;
        case JamStatusStopped:
        default:
            // Red — stopped, solid
            furi_hal_light_set(LightRed,   255);
            furi_hal_light_set(LightGreen, 0);
            furi_hal_light_set(LightBlue,  0);
            break;
    }

    view_port_update(app->view_port);
}

// ---- App lifecycle ----

JammerApp* jammer_app_alloc(void) {
    JammerApp* app = malloc(sizeof(JammerApp));
    if(!app) return NULL;

    app->frequency          = 315000000;
    app->cursor_position    = 2;
    app->jamming_mode       = JammerModeOok650Async;
    app->jam_status         = JamStatusStopped;
    app->screen             = AppScreenJammer;
    app->menu_selection     = 0;
    app->mode_selection     = 0;
    app->preset_selection   = 0;
    app->save_name_selection= 0;
    app->preset_count       = 0;
    app->sweep_enabled      = false;
    app->sweep_freq_a       = 433920000;
    app->sweep_freq_b       = 434000000;
    app->sweep_step_idx     = 1;    // default 50 kHz
    app->sweep_speed_idx    = 1;    // default Medium
    app->sweep_row          = 0;
    app->sweep_last_hop     = 0;
    app->sweep_going_up     = true;
    app->anim_frame         = 0;
    app->app_running        = true;
    app->tx_thread          = NULL;
    app->subghz_txrx        = NULL;
    app->device             = NULL;
    app->timer              = NULL;

    init_default_presets(app);
    furi_hal_region_set(&unlockedRegion);

    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port   = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    subghz_devices_init();
    app->subghz_txrx = subghz_tx_rx_worker_alloc();
    furi_hal_power_suppress_charge_enter();

    // Start animation timer (120 ms → ~8 fps)
    app->timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, 50);

    return app;
}

void jammer_app_free(JammerApp* app) {
    furi_assert(app);

    // Timer first — prevents callbacks during teardown
    if(app->timer) {
        furi_timer_stop(app->timer);
        furi_timer_free(app->timer);
        app->timer = NULL;
    }

    // Turn LED off after timer is stopped so no callback races us
    furi_hal_light_set(LightRed,   0);
    furi_hal_light_set(LightGreen, 0);
    furi_hal_light_set(LightBlue,  0);

    if(app->jam_status != JamStatusStopped) {
        app->jam_status = JamStatusStopped;
        jammer_stop_tx(app);
    }

    if(app->subghz_txrx) {
        if(subghz_tx_rx_worker_is_running(app->subghz_txrx))
            subghz_tx_rx_worker_stop(app->subghz_txrx);
        subghz_tx_rx_worker_free(app->subghz_txrx);
        app->subghz_txrx = NULL;
    }

    if(app->device) {
        if(radio_device_loader_is_external(app->device)) {
            if(furi_hal_power_is_otg_enabled()) furi_hal_power_disable_otg();
        } else {
            radio_device_loader_end(app->device);
        }
        app->device = NULL;
    }

    subghz_devices_deinit();
    furi_hal_power_suppress_charge_exit();

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    free(app);
}

int32_t jammer_app(void* p) {
    UNUSED(p);

    JammerApp* app = jammer_app_alloc();
    if(!app) return -1;

    if(!jammer_init_radio(app)) {
        jammer_app_free(app);
        return -1;
    }

    InputEvent event;
    while(app->app_running) {
        if(furi_message_queue_get(app->event_queue, &event, 10) == FuriStatusOk) {
            switch(app->screen) {
                case AppScreenJammer:     handle_jammer_input(app, &event);    break;
                case AppScreenMenu:       handle_menu_input(app, &event);      break;
                case AppScreenModeSelect: handle_mode_input(app, &event);      break;
                case AppScreenPresets:    handle_presets_input(app, &event);   break;
                case AppScreenSaveName:   handle_save_name_input(app, &event); break;
                case AppScreenSweep:      handle_sweep_input(app, &event);     break;
            }
            view_port_update(app->view_port);
        }

        // Sweep frequency hopping — runs in the main task so subghz API calls are safe
        if(app->sweep_enabled && app->jam_status == JamStatusRunning
           && app->subghz_txrx && app->device) {
            uint32_t now          = furi_get_tick();
            uint32_t interval_ms  = sweep_speeds[app->sweep_speed_idx];
            if((now - app->sweep_last_hop) >= interval_ms) {
                app->sweep_last_hop = now;
                uint32_t step   = sweep_steps[app->sweep_step_idx];
                uint32_t freq_a = app->sweep_freq_a;
                uint32_t freq_b = app->sweep_freq_b;
                if(freq_a > freq_b) { uint32_t t = freq_a; freq_a = freq_b; freq_b = t; }
                uint32_t cur = app->frequency;
                if(app->sweep_going_up) {
                    if(cur + step >= freq_b) { cur = freq_b; app->sweep_going_up = false; }
                    else                     { cur += step; }
                } else {
                    if(cur <= freq_a + step) { cur = freq_a; app->sweep_going_up = true; }
                    else                     { cur -= step; }
                }
                subghz_tx_rx_worker_stop(app->subghz_txrx);
                app->frequency = cur;
                if(app->jam_status == JamStatusRunning)
                    subghz_tx_rx_worker_start(app->subghz_txrx, app->device, cur);
                view_port_update(app->view_port);
            }
        }
    }

    jammer_app_free(app);
    return 0;
}
