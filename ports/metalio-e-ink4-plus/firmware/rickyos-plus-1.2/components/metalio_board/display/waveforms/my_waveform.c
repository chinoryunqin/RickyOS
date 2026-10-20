/**
 * @brief MY_WAVEFORM：完整 LUT 头 + 可选开机裁剪（对齐 pico E0470_WAVEFORM）
 */

#include "my_waveform.h"

#include "my_waveform_du.h"
#include "my_waveform_gc.h"
#include "my_waveform_gl.h"
#include "my_waveform_trim.h"

#include <assert.h>
#include <string.h>

#include <esp_log.h>

#define TAG "my_wf"

// 0=完整 48 相；1=开机裁成 GC16 36 / GL16 37（pico 默认）
#ifndef MY_WAVEFORM_TRIM
#define MY_WAVEFORM_TRIM 0
#endif

#define MY_FULL_DU_FRAMES 20
#define MY_FULL_GC16_FRAMES 48
#define MY_FULL_GL16_FRAMES 48
#define MY_TRIM_ERASE_MAX 11
#define MY_TRIM_SAT_CUT 5
#define MY_TRIM_WHITE_SAT_CUT 0
#define MY_TRIM_HOLD 3
// A normal layout build must not silently include the unaccepted short-waveform
// experiment. Keep it opt-in, and retain every changing-pixel pulse by default.
#ifndef RICKYOS_PLUS_READER_SHORT_WAVEFORM
#define RICKYOS_PLUS_READER_SHORT_WAVEFORM 0
#endif
#if RICKYOS_PLUS_READER_SHORT_WAVEFORM
#define MY_READER_GL16_FRAMES 37
#else
#define MY_READER_GL16_FRAMES MY_FULL_GL16_FRAMES
#endif
#if MY_WAVEFORM_TRIM
#define MY_GC16_FRAMES 36
#define MY_GL16_FRAMES 37
#else
#define MY_GC16_FRAMES MY_FULL_GC16_FRAMES
#define MY_GL16_FRAMES MY_FULL_GL16_FRAMES
#endif

static const EpdWaveformTempInterval my_waveform_intervals[] = {
    { .min = 0, .max = 50 },
};

static inline void lut_or(uint8_t (*data)[16][4], int f, int to, int from, int action) {
    data[f][to][from / 4] |= (uint8_t)(action << (6 - 2 * (from % 4)));
}

static inline int lut_get(const uint8_t (*data)[16][4], int f, int to, int from) {
    return (data[f][to][from / 4] >> (6 - 2 * (from % 4))) & 3;
}

// 源 DU 只认目标 0/15；中间灰 50/50，暗→黑、亮→白
static uint8_t s_du_data[MY_FULL_DU_FRAMES][16][4];
static const EpdWaveformPhases s_du_phases = {
    .phases = MY_FULL_DU_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&s_du_data[0],
};
static const EpdWaveformPhases* s_du_ranges[] = { &s_du_phases };
static const EpdWaveformMode s_du_mode = {
    .type = 1,
    .temp_ranges = 1,
    .range_data = &s_du_ranges[0],
};

static void complete_du_build(void) {
    memset(s_du_data, 0, sizeof(s_du_data));
    for (int to = 0; to < 16; to++) {
        const int to_bin = to < 8 ? 0 : 15;
        for (int from = 0; from < 16; from++) {
            for (int f = 0; f < MY_FULL_DU_FRAMES; f++) {
                const int action = lut_get(my_du_25_0_data, f, to_bin, from);
                if (action != 0) {
                    lut_or(s_du_data, f, to, from, action);
                }
            }
        }
    }
}

// 白底 15→15 源表全保持：挂在已有往白推的一相上再推 1 帧
static void gl16_white_tick(uint8_t (*data)[16][4], int frames) {
    int tick = -1;
    for (int f = frames - 1; f >= 0; f--) {
        for (int from = 0; from < 15; from++) {
            if (lut_get(data, f, 15, from) == 2) {
                tick = f;
                break;
            }
        }
        if (tick >= 0) {
            break;
        }
    }
    if (tick < 0) {
        tick = frames > 2 ? frames - 3 : 0;
    }
    lut_or(data, tick, 15, 15, 2);
}

static uint8_t s_gc16_data[MY_FULL_GC16_FRAMES][16][4];
static uint8_t s_gl16_data[MY_FULL_GL16_FRAMES][16][4];
// Default: full 48-phase source, unchanged pixels held. Optional short experiment:
// 37 phases, still requires optical acceptance. Static 3072/2368-byte LUT only;
// neither path allocates a framebuffer or changes voltage / line timing.
static uint8_t s_reader_gl16_data[MY_READER_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_gc16_phases = {
    .phases = MY_GC16_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&s_gc16_data[0],
};
static const EpdWaveformPhases s_gl16_phases = {
    .phases = MY_GL16_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&s_gl16_data[0],
};
static const EpdWaveformPhases* s_gc16_ranges[] = { &s_gc16_phases };
static const EpdWaveformPhases* s_gl16_ranges[] = { &s_gl16_phases };
static const EpdWaveformMode s_gc16_mode = {
    .type = 2,
    .temp_ranges = 1,
    .range_data = &s_gc16_ranges[0],
};
static const EpdWaveformMode s_gl16_mode = {
    .type = 5,
    .temp_ranges = 1,
    .range_data = &s_gl16_ranges[0],
};
static const EpdWaveformPhases s_reader_gl16_phases = {
    .phases = MY_READER_GL16_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&s_reader_gl16_data[0],
};
static const EpdWaveformPhases* s_reader_gl16_ranges[] = { &s_reader_gl16_phases };
static const EpdWaveformMode s_reader_gl16_mode = {
    // Existing EPDiy mode identifier for the reader-only short experiment.
    .type = 0x6,  // MODE_GL16_FAST in epdiy.h
    .temp_ranges = 1,
    .range_data = &s_reader_gl16_ranges[0],
};

static const EpdWaveformMode* s_modes[] = {
    &s_du_mode,
    &s_gc16_mode,
    &s_gl16_mode,
    &s_reader_gl16_mode,
};

const EpdWaveform MY_WAVEFORM = {
    .num_modes = sizeof(s_modes) / sizeof(s_modes[0]),
    .num_temp_ranges = 1,
    .mode_data = s_modes,
    .temp_intervals = my_waveform_intervals,
};

void my_waveform_init(void) {
    complete_du_build();

#if MY_WAVEFORM_TRIM
    const my_waveform_trim_t trim = {
        .erase_max = MY_TRIM_ERASE_MAX,
        .sat_cut = MY_TRIM_SAT_CUT,
        .white_sat_cut = MY_TRIM_WHITE_SAT_CUT,
        .hold = MY_TRIM_HOLD,
    };
    const int gc = my_waveform_trim(&my_gc_25_0, &trim, s_gc16_data);
    const int gl = my_waveform_trim(&my_gl_25_0, &trim, s_gl16_data);
    assert(gc == MY_GC16_FRAMES);
    assert(gl == MY_GL16_FRAMES);
    ESP_LOGD(TAG, "trimmed GC16=%d GL16=%d DU=%d (from full 48/48/20)", gc, gl, MY_FULL_DU_FRAMES);
#else
    memcpy(s_gc16_data, my_gc_25_0_data, sizeof(s_gc16_data));
    memcpy(s_gl16_data, my_gl_25_0_data, sizeof(s_gl16_data));
    ESP_LOGD(TAG, "full GC16=%d GL16=%d DU=%d (trim off)", MY_GC16_FRAMES, MY_GL16_FRAMES,
             MY_FULL_DU_FRAMES);
#endif
    // Full and trimmed GL16 both need the unchanged-white background pulse.
    gl16_white_tick(s_gl16_data, MY_GL16_FRAMES);
#if RICKYOS_PLUS_READER_SHORT_WAVEFORM
    const my_waveform_trim_t reader_trim = {
        .erase_max = MY_TRIM_ERASE_MAX,
        .sat_cut = MY_TRIM_SAT_CUT,
        .white_sat_cut = MY_TRIM_WHITE_SAT_CUT,
        .hold = MY_TRIM_HOLD,
    };
    // trim's destination contract requires space for the full source. Reuse
    // the existing full-size GL scratch at boot, then restore it byte-for-byte.
    // No new 48-phase staging buffer or heap allocation.
    const int reader_frames = my_waveform_trim(&my_gl_25_0, &reader_trim, s_gl16_data);
    assert(reader_frames == MY_READER_GL16_FRAMES);
    memcpy(s_reader_gl16_data, s_gl16_data, sizeof(s_reader_gl16_data));
#if MY_WAVEFORM_TRIM
    const int restored = my_waveform_trim(&my_gl_25_0, &reader_trim, s_gl16_data);
    assert(restored == MY_GL16_FRAMES);
#else
    memcpy(s_gl16_data, my_gl_25_0_data, sizeof(s_gl16_data));
#endif
    gl16_white_tick(s_gl16_data, MY_GL16_FRAMES);
#else
    memcpy(s_reader_gl16_data, my_gl_25_0_data, sizeof(s_reader_gl16_data));
#endif
    for (int f = 0; f < MY_READER_GL16_FRAMES; ++f) {
        for (int gray = 0; gray < 16; ++gray) {
            s_reader_gl16_data[f][gray][gray / 4] &=
                (uint8_t)~(3u << (6 - 2 * (gray % 4)));
        }
    }
    ESP_LOGD(TAG, "reader GL16=%d (short experiment=%d)",
             MY_READER_GL16_FRAMES, RICKYOS_PLUS_READER_SHORT_WAVEFORM);
}
