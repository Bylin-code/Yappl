#include "drivers/inmp441_microphone.h"

#include <algorithm>
#include <limits>

#include <esp_idf_version.h>

#include "app/config.h"

namespace yappl {
namespace {

constexpr i2s_port_t kMicPort = I2S_NUM_0;

i2s_comm_format_t standardI2sFormat() {
#if ESP_IDF_VERSION_MAJOR >= 4
  return I2S_COMM_FORMAT_STAND_I2S;
#else
  return I2S_COMM_FORMAT_I2S;
#endif
}

}  // namespace

bool Inmp441Microphone::begin(uint32_t sampleRateHz) {
  if (started_) {
    return true;
  }

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = sampleRateHz;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = standardI2sFormat();
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 4;
  config.dma_buf_len = AppConfig::micSampleCount;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = AppConfig::micBclkPin;
  pins.ws_io_num = AppConfig::micLrclkPin;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = AppConfig::micDataPin;

  if (i2s_driver_install(kMicPort, &config, 0, nullptr) != ESP_OK) {
    return false;
  }
  if (i2s_set_pin(kMicPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kMicPort);
    return false;
  }
  if (i2s_zero_dma_buffer(kMicPort) != ESP_OK) {
    i2s_driver_uninstall(kMicPort);
    return false;
  }

  sampleRateHz_ = sampleRateHz;
  started_ = true;
  return true;
}

size_t Inmp441Microphone::read(int32_t *samples, size_t sampleCount) {
  if (!started_ || samples == nullptr || sampleCount == 0) {
    return 0;
  }

  size_t bytesRead = 0;
  const size_t bytesRequested = sampleCount * sizeof(samples[0]);
  const esp_err_t result = i2s_read(kMicPort, samples, bytesRequested, &bytesRead, pdMS_TO_TICKS(100));
  if (result != ESP_OK) {
    return 0;
  }

  return bytesRead / sizeof(samples[0]);
}

bool Inmp441Microphone::readLevel(int32_t *scratch, size_t sampleCount, MicLevelStats &stats) {
  const size_t samplesRead = read(scratch, sampleCount);
  if (samplesRead == 0) {
    return false;
  }

  int32_t minimum = std::numeric_limits<int32_t>::max();
  int32_t maximum = std::numeric_limits<int32_t>::min();

  for (size_t i = 0; i < samplesRead; ++i) {
    // INMP441 places 24-bit signed data in a 32-bit slot. For a responsive
    // visual meter the raw span is enough; no PCM conversion is needed here.
    minimum = std::min(minimum, scratch[i]);
    maximum = std::max(maximum, scratch[i]);
  }

  int64_t span = static_cast<int64_t>(maximum) - static_cast<int64_t>(minimum);
  span -= AppConfig::noiseFloor;
  span = std::max<int64_t>(0, span);
  span = std::min<int64_t>(AppConfig::noiseCeiling, span);

  stats.minimum = minimum;
  stats.maximum = maximum;
  stats.span = static_cast<int32_t>(span);
  stats.level = static_cast<uint8_t>(span * 100 / AppConfig::noiseCeiling);
  return true;
}

bool Inmp441Microphone::sanityCheck() {
  int32_t samples[AppConfig::micSampleCount] = {};
  MicLevelStats stats;
  if (!readLevel(samples, AppConfig::micSampleCount, stats)) {
    Serial.println(F("INMP441 sanity check failed: no samples read"));
    return false;
  }

  Serial.printf("INMP441 min=%ld max=%ld span=%ld level=%u%%\n",
                static_cast<long>(stats.minimum),
                static_cast<long>(stats.maximum),
                static_cast<long>(stats.span),
                stats.level);
  return true;
}

void Inmp441Microphone::end() {
  if (!started_) {
    return;
  }

  i2s_driver_uninstall(kMicPort);
  started_ = false;
  sampleRateHz_ = 0;
}

}  // namespace yappl
