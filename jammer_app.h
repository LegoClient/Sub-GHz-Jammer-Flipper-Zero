#pragma once

#include <gui/gui.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/subghz_tx_rx_worker.h>
#include <stdint.h>

#define MAX_PRESETS          20
#define PRESET_NAME_LEN      14
#define MENU_ITEMS_COUNT      6
#define MODE_LIST_VISIBLE     5
#define PRESETS_LIST_VISIBLE  4
#define SAVE_NAMES_COUNT     17
#define SWEEP_STEP_COUNT      4
#define SWEEP_SPEED_COUNT     3
#define MAIN_STYLE_COUNT      6

typedef enum {
    JammerModeOok650Async = 0,
    JammerMode2FSKDev238Async,
    JammerMode2FSKDev476Async,
    JammerModeMSK99_97KbAsync,
    JammerModeGFSK9_99KbAsync,
    JammerModeBruteforce,
    JammerModeSineWave,
    JammerModeSquareWave,
    JammerModeSawtoothWave,
    JammerModeWhiteNoise,
    JammerModeTriangleWave,
    JammerModeChirp,
    JammerModeGaussianNoise,
    JammerModeBurst,
    JammerModeRawNoise,
    JammerModeCount,
} JammerMode;

typedef enum {
    JamStatusStopped,
    JamStatusRunning,
    JamStatusPaused,
} JamStatus;

typedef enum {
    AppScreenJammer,
    AppScreenMenu,
    AppScreenModeSelect,
    AppScreenPresets,
    AppScreenSaveName,
    AppScreenSweep,
    AppScreenStyleSelect,
} AppScreen;

typedef enum {
    StyleClassic = 0,
    StyleOscilloscope,
    StyleRadar,
    StyleTuner,
    StyleTerminal,
    StyleSpectrum,
} MainScreenStyle;

typedef struct {
    char       name[PRESET_NAME_LEN];
    uint32_t   frequency;
    JammerMode mode;
} Preset;

typedef struct {
    Gui*              gui;
    ViewPort*         view_port;
    FuriMessageQueue* event_queue;
    FuriTimer*        timer;

    const SubGhzDevice* device;
    SubGhzTxRxWorker*   subghz_txrx;
    FuriThread*         tx_thread;

    uint32_t   frequency;
    uint8_t    cursor_position;
    JammerMode jamming_mode;
    JamStatus  jam_status;
    AppScreen  screen;

    uint8_t menu_selection;
    uint8_t mode_selection;
    uint8_t preset_selection;
    uint8_t save_name_selection;

    Preset  presets[MAX_PRESETS];
    uint8_t preset_count;

    // Sweep
    bool     sweep_enabled;
    uint32_t sweep_freq_a;     // start of sweep range
    uint32_t sweep_freq_b;     // end of sweep range
    uint8_t  sweep_step_idx;   // index into sweep_steps[]
    uint8_t  sweep_speed_idx;  // index into sweep_speeds[]
    uint8_t  sweep_row;        // 0=FreqB, 1=Step, 2=Speed
    uint32_t sweep_last_hop;   // furi_get_tick() at last frequency hop
    bool     sweep_going_up;   // current sweep direction

    // Main-screen visual style
    MainScreenStyle main_style;
    uint8_t         style_selection;

    uint8_t anim_frame;
    bool    app_running;
} JammerApp;

JammerApp* jammer_app_alloc(void);
void       jammer_app_free(JammerApp* app);
int32_t    jammer_app(void* p);
