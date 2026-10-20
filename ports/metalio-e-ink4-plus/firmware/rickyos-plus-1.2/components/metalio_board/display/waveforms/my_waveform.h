#pragma once

#include "epd_internals.h"

/** GC16/GL16 + 完备 DU；GL16_FAST 为阅读专用 37 相裁剪/保持对角线实验表。 */
extern const EpdWaveform MY_WAVEFORM;

/**
 * @brief 开机装配 MY_WAVEFORM（DU 完备；GC/GL 按 MY_WAVEFORM_TRIM 裁或拷全表）
 * @note 须在 epd_hl_init(&MY_WAVEFORM) 之前调用一次
 */
void my_waveform_init(void);
