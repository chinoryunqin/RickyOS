#pragma once
#include <stdint.h>
#include <stdbool.h>

// One atomic word: reason[7:0], producer[15:8], row[31:16]. Unknown is
// producer=255 / row=65535. Sticky until reboot, including LCD reconstruction.
typedef enum {
    LCD_FAULT_ROW_NOT_READY = 1,
    LCD_FAULT_QUEUE_EMPTY = 2,
    LCD_FAULT_FRAME_SHORT = 3,
    LCD_FAULT_FRAME_TIMEOUT = 4,
    LCD_FAULT_FEED_TIMEOUT = 5,
    LCD_FAULT_QUEUE_TIMEOUT = 6,
    LCD_FAULT_PREFILL_TIMEOUT = 7,
} EpdLcdFaultReason;

static inline __attribute__((always_inline)) void lcd_note_first_fault(
    uint32_t* record, EpdLcdFaultReason reason, int row, int producer
) {
    const uint32_t line = row >= 0 && row < 65535 ? (uint32_t)row : 65535u;
    const uint32_t worker = producer >= 0 && producer < 255 ? (uint32_t)producer : 255u;
    uint32_t expected = 0;
    const uint32_t value = (line << 16) | (worker << 8) | (uint32_t)reason;
    __atomic_compare_exchange_n(record, &expected, value, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

#ifdef __cplusplus
extern "C" {
#endif
uint32_t epd_lcd_get_first_fault(void);
#ifdef __cplusplus
}
#endif
