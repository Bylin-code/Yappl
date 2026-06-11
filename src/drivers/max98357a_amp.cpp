#include "drivers/max98357a_amp.h"

#include <esp_idf_version.h>

#include "app/config.h"

namespace yappl {
namespace {

constexpr i2s_port_t kAmpPort = I2S_NUM_1;

i2s_comm_format_t standardI2sFormat() {
#if ESP_IDF_VERSION_MAJOR >= 4
  return I2S_COMM_FORMAT_STAND_I2S;
#else
  return I2S_COMM_FORMAT_I2S;
#endif
}

}  // namespace

bool Max98357aAmp::begin(uint32_t sampleRateHz) {
  if (started_) {
    return true;
  }

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = sampleRateHz;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = standardI2sFormat();
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 4;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = AppConfig::ampBclkPin;
  pins.ws_io_num = AppConfig::ampLrclkPin;
  pins.data_out_num = AppConfig::ampDataPin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(kAmpPort, &config, 0, nullptr) != ESP_OK) {
    return false;
  }
  if (i2s_set_pin(kAmpPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kAmpPort);
    return false;
  }
  if (i2s_zero_dma_buffer(kAmpPort) != ESP_OK) {
    i2s_driver_uninstall(kAmpPort);
    return false;
  }

  sampleRateHz_ = sampleRateHz;
  started_ = true;
  return true;
}

size_t Max98357aAmp::write(const int16_t *samples, size_t sampleCount) {
  if (!started_ || samples == nullptr || sampleCount == 0) {
    return 0;
  }

  size_t bytesWritten = 0;
  const size_t bytesRequested = sampleCount * sizeof(samples[0]);
  const esp_err_t result = i2s_write(kAmpPort, samples, bytesRequested, &bytesWritten, pdMS_TO_TICKS(100));
  if (result != ESP_OK) {
    return 0;
  }

  return bytesWritten / sizeof(samples[0]);
}

void Max98357aAmp::end() {
  if (!started_) {
    return;
  }

  i2s_driver_uninstall(kAmpPort);
  started_ = false;
  sampleRateHz_ = 0;
}

}  // namespace yappl
