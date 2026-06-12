#include "drivers/inmp441_microphone.h"

#include <algorithm>
#include <limits>

#include <esp_idf_version.h>

#include "app/config.h"

namespace yappl {
namespace {

constexpr i2s_port_t kMicPort = I2S_NUM_0;
constexpr uint8_t kInmp441SlotShift = 8;

// Arduino-ESP32 can be built against different ESP-IDF versions. Hide the I2S
// format enum difference here so the rest of the driver stays stable.
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

  // INMP441 is a receive-only I2S device. LR is wired low in this project, so
  // the mic data is on the left channel.
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

  // i2s_read may block while waiting for enough DMA samples. Keep mic reads out
  // of timing-sensitive tasks.
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
  int32_t peakVolume = 0;

  for (size_t i = 0; i < samplesRead; ++i) {
    // INMP441 places 24-bit signed audio in a 32-bit I2S slot. Shift into the
    // normal signed 24-bit range so a full-scale peak is about +/-8388607.
    const int32_t sample = scratch[i] >> kInmp441SlotShift;
    const int32_t volume = sample < 0 ? -sample : sample;

    minimum = std::min(minimum, sample);
    maximum = std::max(maximum, sample);
    peakVolume = std::max(peakVolume, volume);
  }

  // The meter uses peak magnitude, not calibrated SPL/VU behavior. It is meant
  // to be visually responsive during bring-up.
  int64_t meterValue = static_cast<int64_t>(peakVolume) - AppConfig::noiseFloor;
  meterValue = std::max<int64_t>(0, meterValue);
  meterValue = std::min<int64_t>(AppConfig::noiseCeiling, meterValue);

  stats.minimum = minimum;
  stats.maximum = maximum;
  stats.span = static_cast<int32_t>(meterValue);
  stats.level = static_cast<uint8_t>(meterValue * 100 / AppConfig::noiseCeiling);
  if (meterValue > 0 && stats.level == 0) {
    stats.level = 1;
  }
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
