#include "PlusAudio.h"
#include <HardwareSerial.h>
#include <config.h>
#include <detail/MetalioPcm.h>
#include <driver/i2s_std.h>
#include <epd_board_metalio_eink4_plus.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace plus_audio {
namespace {
std::atomic<bool> running{false}, stopping{false};
std::atomic<uint8_t> gain{35};
HalAudioOutput::ReadCallback reader = nullptr;
uint8_t channelCount = 1;
size_t tonePosition = 0;
// Quiet 1 kHz preview, 300 ms at 16 kHz. No file or network dependency.
int toneRead(uint8_t* dst, size_t length) {
  constexpr int16_t wave[] = {0, 1148, 2121, 2772, 3000, 2772, 2121, 1148,
                            0, -1148, -2121, -2772, -3000, -2772, -2121, -1148};
  const size_t samples = std::min(length / 2, 4800 - tonePosition);
  for (size_t i = 0; i < samples; ++i) {
    const auto sample = wave[(tonePosition + i) % 16];
    memcpy(dst + i * 2, &sample, 2);
  }
  tonePosition += samples;
  return samples * 2;
}
void settle(unsigned ms) {
  for (unsigned elapsed = 0; elapsed < ms && !stopping.load(); elapsed += 50)
    vTaskDelay(pdMS_TO_TICKS(50));
}
void worker(void*) {
  HardwareSerial uart(1);
  i2s_chan_handle_t tx = nullptr;
  bool enabled = false;
  size_t total = 0;
  epd_board_metalio_eink4_plus_pa_set(false);
  uart.begin(115200, SERIAL_8N1, BT_AUDIO_RX_PIN, BT_AUDIO_TX_PIN);
  bool uartOk = uart.print("AT+RX=2\r\n") == 9; uart.flush();
  settle(700);
  uartOk = (uart.print("AT+MODE=1\r\n") == 11) && uartOk; uart.flush();
  settle(500);
  ESP_LOGI("PlusAudio", "BH1098 mode1 RX bytes=%d", uart.available());
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
  chan.auto_clear = true;
  chan.dma_desc_num = 4;
  chan.dma_frame_num = 128;
  esp_err_t err = uartOk ? ESP_OK : ESP_FAIL;
  if (!stopping.load() && err == ESP_OK) err = i2s_new_channel(&chan, &tx, nullptr);
  if (tx && err == ESP_OK) {
    i2s_std_config_t cfg = {};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.bclk = AUDIO_I2S_BCLK;
    cfg.gpio_cfg.ws = AUDIO_I2S_WS;
    cfg.gpio_cfg.dout = AUDIO_I2S_DOUT;
    cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    err = i2s_channel_init_std_mode(tx, &cfg);
    if (err == ESP_OK) { err = i2s_channel_enable(tx); enabled = err == ESP_OK; }
  }
  if (enabled && !stopping.load()) {
    bool ampOn = gain.load() != 0;
    epd_board_metalio_eink4_plus_pa_set(ampOn);
    // Small streaming blocks; partial callback reads are reassembled into whole PCM frames.
    uint8_t pcm[256];
    int32_t slots[256];
    size_t buffered = 0;
    while (!stopping.load()) {
      const int n = reader(pcm + buffered, sizeof(pcm) - buffered);
      if (n < 0 || static_cast<size_t>(n) > sizeof(pcm) - buffered) { err = ESP_FAIL; break; }
      buffered += n;
      const size_t frameBytes = 2 * channelCount;
      const size_t frames = buffered / frameBytes;
      const uint8_t volume = gain.load();
      if ((volume != 0) != ampOn) {
        ampOn = volume != 0;
        epd_board_metalio_eink4_plus_pa_set(ampOn);
      }
      for (size_t i = 0; i < frames; ++i) {
        int16_t left, right;
        memcpy(&left, pcm + i * frameBytes, 2);
        right = left;
        if (channelCount == 2) memcpy(&right, pcm + i * frameBytes + 2, 2);
        slots[2 * i] = freeink::metalio::outputSample(left, volume);
        slots[2 * i + 1] = freeink::metalio::outputSample(right, volume);
      }
      size_t offset = 0, bytes = frames * 8;
      while (offset < bytes && !stopping.load()) {
        size_t written = 0;
        err = i2s_channel_write(tx, reinterpret_cast<uint8_t*>(slots) + offset, bytes - offset, &written, 250);
        offset += written;
        total += written;
        if (written == 0 && err == ESP_OK) err = ESP_FAIL;
        if (err != ESP_OK) break;
      }
      if (err != ESP_OK || offset < bytes) break;
      const size_t used = frames * frameBytes;
      buffered -= used;
      memmove(pcm, pcm + used, buffered);
      if (n == 0) { if (buffered) err = ESP_ERR_INVALID_SIZE; break; }
    }
    // Let the final DMA block leave the speaker before disabling PA.
    if (!stopping.load() && err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(40));
  }
  epd_board_metalio_eink4_plus_pa_set(false);
  if (enabled) i2s_channel_disable(tx);
  if (tx) i2s_del_channel(tx);
  uart.end();
  ESP_LOGI("PlusAudio", "Playback finished bytes=%u result=%s cancelled=%d", static_cast<unsigned>(total),
           esp_err_to_name(err), stopping.load());
  running.store(false);
  vTaskDelete(nullptr);
}
}
bool play(HalAudioOutput::ReadCallback read, uint32_t rate, uint8_t channels, uint8_t volume) {
  if (!read || rate != 16000 || (channels != 1 && channels != 2) || running.exchange(true)) return false;
  reader = read;
  channelCount = channels;
  gain.store(std::min<uint8_t>(volume, 100));
  stopping.store(false);
  // Allocated only while playing: 4 KiB task stack + IDF I2S DMA buffers (~4 KiB).
  // Background task keeps the first-use UART settling and I2S waits off the UI thread.
  if (xTaskCreate(worker, "plus_audio", 4096, nullptr, 5, nullptr) != pdPASS) {
    running.store(false);
    ESP_LOGE("PlusAudio", "Cannot allocate playback task");
    return false;
  }
  return true;
}
void setVolume(uint8_t volume) { gain.store(std::min<uint8_t>(volume, 100)); }
void shutdown() {
  stopping.store(true);
  // UART settle is bounded at 1.2 s, I2S writes at 250 ms. Worker owns cleanup.
  while (running.load()) vTaskDelay(pdMS_TO_TICKS(1));
  epd_board_metalio_eink4_plus_pa_set(false);
}
bool preview(uint8_t volume) {
  if (running.load()) return false;
  tonePosition = 0;
  return play(toneRead, 16000, 1, volume);
}
}
