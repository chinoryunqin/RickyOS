// SPDX-License-Identifier: LGPL-3.0-or-later
// Included by render.c; all scratch is owned until both feeders have joined.
#include "ricky_native_turn.h"
static RickyNativeTurn s_next_native_turn;
static RickyNativeTurnRegion s_next_native_region;
static bool s_next_reader_text_finish;
#if defined(CONFIG_IDF_TARGET_ESP32S31)
// This renderer has one serialized context. Reserve its bounded hot working set
// in HP DRAM at link time, not in the fragmented reading heap. S31's C feeder
// needs only the low action byte of each padded 32-bit LUT entry: 9 * 256 B.
// No full-frame/static phase bank, and no ISR accesses this producer-only table.
static DRAM_ATTR uint8_t s_native_turn_window[9 * 256] __attribute__((aligned(16)));
#endif

void epd_set_native_turn(uint8_t style, uint8_t orientation, bool from_right) {
    s_next_native_turn = (RickyNativeTurn){.style=style <= 3 ? style : 0, .orientation=orientation, .from_right=from_right};
    s_next_native_region = (RickyNativeTurnRegion){0};
    s_next_reader_text_finish = false;
}

void epd_set_native_reader_text_finish(bool enabled) { s_next_reader_text_finish = enabled; }

void epd_set_native_ripple_origin(uint16_t x, uint16_t y) {
    if (s_next_native_turn.style != 3 || x > 1023 || y > 1023) return;
    s_next_native_turn.has_origin = true;
    s_next_native_turn.origin_x = x;
    s_next_native_turn.origin_y = y;
}

void epd_set_native_ui_turn(uint8_t orientation, bool from_right, int x, int y, int width, int height) {
    epd_set_native_turn(1, orientation, from_right);
    s_next_native_region = (RickyNativeTurnRegion){x, y, width, height, RICKY_UI_TURN_SPREAD};
}

// Align the end, not the start, of each native UI transition. Black/white UI
// strokes must not finish early and then wait for the long gray sequences.
// Move leading/trailing HOLD only; retain every active action, its order and
// every internal HOLD. Reader LUTs and the calibrated waveform are untouched.
static void ricky_ui_lock_completion(uint8_t* bank, int phases) {
    for (int pair = 0; pair < 256; ++pair) {
        int last = phases - 1;
        while (last >= 0 && bank[last * 1024 + pair * 4] == 0) --last;
        if (last < 0) continue; // unchanged/HOLD-only transitions stay inert
        // Retain the calibrated three final HOLD phases for settling; do not
        // trade those for an earlier animation. Unexpected tables stay as-is.
        // Plus UI uses the accepted reader's black-ink settling interval:
        // pure black lands four ticks before gray edges (seven final HOLDs).
        // Preserve every pulse and internal HOLD; do not add a refresh/phase.
        const int to = pair >> 4;
        const int finish = phases - (to == 0 ? 8 : 4);
        const int shift = finish - last;
        if (shift <= 0) continue;
        for (int tick = phases - 1; tick >= 0; --tick) {
            uint32_t action = 0;
            if (tick >= shift) memcpy(&action, bank + (tick - shift) * 1024 + pair * 4, sizeof(action));
            memcpy(bank + tick * 1024 + pair * 4, &action, sizeof(action));
        }
    }
}

static int ricky_ui_trim_idle_head(uint8_t* bank, int phases) {
    int head = 0;
    for (; head < phases; ++head) {
        bool active = false;
        for (int pair = 0; pair < 256; ++pair) {
            if (bank[head * 1024 + pair * 4]) { active = true; break; }
        }
        if (active) break;
    }
    if (head == 0 || head == phases) return phases;
    // All 256 transitions HOLD here, not just the currently visible pixels.
    // Omit only that global idle prefix; keep the three final settling HOLDs.
    memmove(bank, bank + head * 1024, (size_t)(phases - head) * 1024);
    return phases - head;
}

// Pure AA text only: changing black/gray strokes finish near their gray edges.
// Move only exterior HOLD: keep every original active action and internal HOLD,
// at least three final settling ticks, unchanged-pixel cells (including the accepted
// retained-black re-drive), and all transitions toward white exactly intact.
// This is a timing A/B experiment, not a claim of eliminating optical overshoot.
static void ricky_reader_lock_text_finish(uint8_t* bank, int phases) {
    for (int pair = 0; pair < 256; ++pair) {
        const int to = pair >> 4, from = pair & 15;
        if (to == 15 || to == from) continue;
        int last = phases - 1;
        while (last >= 0 && bank[last * 1024 + pair * 4] == 0) --last;
        if (last < 0) continue;
        // A/B after users liked convergence but reported lighter ink: let only
        // solid black finish four ticks before the gray edge (7 trailing HOLDs).
        // Same pulses/order/count, no additional drive, voltage or cycle ticks.
        const int finish = phases - (to == 0 ? 8 : 4);
        const int shift = finish - last;
        if (shift <= 0) continue;
        for (int tick = phases - 1; tick >= 0; --tick) {
            uint32_t action = 0;
            if (tick >= shift) memcpy(&action, bank + (tick - shift) * 1024 + pair * 4, sizeof(action));
            memcpy(bank + tick * 1024 + pair * 4, &action, sizeof(action));
        }
    }
}

static void ricky_native_end(RenderContext_t* ctx) {
    heap_caps_free(ctx->turn_luts);
    heap_caps_free(ctx->turn_delays);
#if !defined(CONFIG_IDF_TARGET_ESP32S31)
    heap_caps_free(ctx->turn_window);
#endif
    ctx->turn_luts = NULL;
    ctx->turn_delays = NULL;
    ctx->turn_window = NULL;
    ctx->turn_phases = 0;
    ctx->turn_region = (RickyNativeTurnRegion){0};
    ctx->turn_producer_tick = 0;
    ctx->reader_text_finish = false;
}

static void ricky_native_begin(RenderContext_t* ctx, const EpdWaveformPhases* phases,
                               RickyNativeTurn turn) {
    ctx->turn_luts = NULL;
    ctx->turn_delays = NULL;
    ctx->turn_window = NULL;
    ctx->turn_phases = 0;
    // Only differential full-screen native reader waves. Variable phase timing
    // cannot be staggered without changing each pixel's drive integral.
    const LutFunctionPair compact = find_lut_functions(ctx->mode, 1024);
    if ((!turn.style && !ctx->reader_text_finish) || turn.style > 3 || turn.orientation > 3 || !phases || phases->phases < 2 ||
        phases->phases > 64 || !(ctx->mode & MODE_PACKING_1PPB_DIFFERENCE) ||
        compact.build_func != ctx->lut_build_func || ctx->area.x || ctx->area.y ||
        ctx->area.width != ctx->display_width || ctx->area.height != ctx->display_height ||
        ctx->crop_to.x || ctx->crop_to.y ||
        ctx->crop_to.width != ctx->display_width || ctx->crop_to.height != ctx->display_height)
        return;
    if (phases->phase_times) {
        for (int i = 1; i < phases->phases; ++i)
            if (phases->phase_times[i] != phases->phase_times[0]) return;
    }
    const RickyNativeTurnRegion region = ctx->turn_region;
    if (region.spread && (region.spread > RICKY_UI_TURN_SPREAD || region.x < 0 || region.y < 0 ||
        region.width <= 0 || region.height <= 0 ||
        region.width > ctx->display_width || region.height > ctx->display_height ||
        region.x > ctx->display_width - region.width ||
        region.y > ctx->display_height - region.height)) return;
    const int cols = (ctx->display_width + 15) / 16;
    const int rows = (ctx->display_height + 15) / 16;
    // Optional, turn-scoped scratch; no extra framebuffer. Reading multiple hot
    // phase LUTs from PSRAM on every UI row misses the feeder deadline. Cache
    // only the simultaneously active phase window internally, not the whole bank.
    // S31 reserves 2304 B HP DRAM and depads to 256 B/phase (reader uses 2048 B).
    // Other targets retain their existing 1 KiB/phase converter. C owns scratch until both
    // feeders join; OOM falls back before any drive, never to a half animation.
    ctx->turn_luts = heap_caps_aligned_alloc(16, (size_t)phases->phases * 1024, MALLOC_CAP_SPIRAM);
    ctx->turn_delays = heap_caps_malloc((size_t)cols * rows, MALLOC_CAP_SPIRAM);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    ctx->turn_window = s_native_turn_window;
#else
    ctx->turn_window = heap_caps_aligned_alloc(16, ((region.spread ? region.spread : 7) + 1) * 1024,
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
#endif
    if (!ctx->turn_luts || !ctx->turn_delays || !ctx->turn_window) {
        ESP_LOGW("NativeTurn", "Scratch unavailable; ordinary refresh");
        ricky_native_end(ctx);
        return;
    }
    for (int i = 0; i < phases->phases; ++i)
        ctx->lut_build_func(ctx->turn_luts + i * 1024, phases, i);
    int nativePhases = phases->phases;
    if (region.spread) {
        ricky_ui_lock_completion(ctx->turn_luts, nativePhases);
        nativePhases = ricky_ui_trim_idle_head(ctx->turn_luts, nativePhases);
    } else if (ctx->reader_text_finish) {
        ricky_reader_lock_text_finish(ctx->turn_luts, nativePhases);
    }
    const RickyRippleGeometry ripple = turn.style == 3 ? ricky_ripple_geometry(turn,ctx->display_width,ctx->display_height)
                                                       : (RickyRippleGeometry){0};
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const int px = x * 16 + 8 < ctx->display_width ? x * 16 + 8 : ctx->display_width - 1;
            const int py = y * 16 + 8 < ctx->display_height ? y * 16 + 8 : ctx->display_height - 1;
            int delay;
            if (region.spread) {
                const int rx = px < region.x ? 0 :
                    px >= region.x + region.width ? region.width - 1 : px - region.x;
                const int ry = py < region.y ? 0 :
                    py >= region.y + region.height ? region.height - 1 : py - region.y;
                delay = ricky_ui_turn_delay(turn, rx, ry, region.width, region.height, region.spread);
            } else if (!turn.style) {
                delay = 0; // AA convergence is available with spatial motion OFF.
            } else {
                delay = turn.style == 3 ? ricky_turn_ease(ricky_ripple_axis(ripple,px,py))
                                        : ricky_turn_delay(turn, px, py, ctx->display_width, ctx->display_height);
            }
            ctx->turn_delays[y * cols + x] = delay;
        }
    }
    ctx->turn_phases = nativePhases;
    ctx->turn_cols = cols;
    ctx->current_frame = 0;
    ctx->turn_producer_tick = 0;
    ctx->cycle_frames = nativePhases + (region.spread ? region.spread : turn.style ? 7 : 0);
    ESP_LOGI("NativeTurn", "style=%u orientation=%u right=%d phases=%d ticks=%d scratch=%u",
             turn.style, turn.orientation, turn.from_right, ctx->turn_phases, ctx->cycle_frames,
             (unsigned)((size_t)phases->phases * 1024 + cols * rows));
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    ESP_LOGI("NativeTurn", "hot=%p cpu=%dMHz", (void*)ctx->turn_window, esp_clk_cpu_freq() / 1000000);
#endif
    if (ctx->reader_text_finish) ESP_LOGI("ReaderFinish", "completion-aligned text; phases=%d ticks=%d", nativePhases, ctx->cycle_frames);
}
