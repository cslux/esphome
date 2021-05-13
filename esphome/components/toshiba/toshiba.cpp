#include "toshiba.h"
#include "esphome/core/log.h"

namespace esphome {
namespace toshiba {

const uint8_t TOSHIBA_FRAME_MAX_LENGTH = 14;
const uint8_t TOSHIBA_FRAME_LENGTH_NO_DATA = 6;

const uint16_t TOSHIBA_HEADER_MARK = 4380;
const uint16_t TOSHIBA_HEADER_SPACE = 4370;
const uint16_t TOSHIBA_GAP_SPACE = 5480;
const uint16_t TOSHIBA_BIT_MARK = 540;
const uint16_t TOSHIBA_ZERO_SPACE = 540;
const uint16_t TOSHIBA_ONE_SPACE = 1620;

const uint8_t TOSHIBA_COMMAND_DEFAULT = 0x01;
const uint8_t TOSHIBA_COMMAND_TIMER = 0x03;
const uint8_t TOSHIBA_COMMAND_POWER = 0x09;
const uint8_t TOSHIBA_COMMAND_COMFORT_SLEEP = 0x0b;
const uint8_t TOSHIBA_COMMAND_MOTION = 0x21;

const uint8_t TOSHIBA_MODE_AUTO = 0x00;
const uint8_t TOSHIBA_MODE_COOL = 0x01;
const uint8_t TOSHIBA_MODE_DRY = 0x02;
const uint8_t TOSHIBA_MODE_HEAT = 0x03;
/* Sets temp to 22 ̊ C */
const uint8_t TOSHIBA_MODE_FAN_ONLY = 0x04;
const uint8_t TOSHIBA_MODE_OFF = 0x07;

/* Fan mode values are left shifted by 4 */
const uint8_t TOSHIBA_FAN_SPEED_AUTO = 0x00;
const uint8_t TOSHIBA_FAN_SPEED_QUIET = 0x20;
const uint8_t TOSHIBA_FAN_SPEED_1 = 0x40;
const uint8_t TOSHIBA_FAN_SPEED_2 = 0x60;
const uint8_t TOSHIBA_FAN_SPEED_3 = 0x80;
const uint8_t TOSHIBA_FAN_SPEED_4 = 0xa0;
const uint8_t TOSHIBA_FAN_SPEED_5 = 0xc0;

const uint8_t TOSHIBA_POWER_HIGH = 0x01;
/* ECO/Comfort Sleep */
const uint8_t TOSHIBA_POWER_ECO = 0x03;
/* Sets temp to 23 ̊ C, and fan mode to AUTO */
const uint8_t TOSHIBA_POWER_ONE_TOUCH = 0x07;

const uint8_t TOSHIBA_POWER_SEL_100 = 0x00;
const uint8_t TOSHIBA_POWER_SEL_75 = 0x04;
const uint8_t TOSHIBA_POWER_SEL_50 = 0x08;

const uint8_t TOSHIBA_MOTION_FIX = 0x00;
/* Default value for the models supporting vertical mode only  */
uint8_t toshiba_motion_swing_vertical = 0x01;
uint8_t toshiba_motion_swing_off = 0x02;
/* Value 0xff is just a dummy value, actual value will be set in the setup() method */
uint8_t toshiba_motion_swing_both = 0xff;
/* Default value for the Model::MODEL_WH_TA01LE */
uint8_t toshiba_motion_swing_horizontal = 0x05;

static const char* TAG = "toshiba.climate";

static void log_frame(const uint8_t* frame) {
  static constexpr char HEX_NUMBERS[] = "0123456789ABCDEF";
  uint8_t frame_length = TOSHIBA_FRAME_LENGTH_NO_DATA + frame[2];
  std::string frame_as_hex_string;

  /* Two digits per character */
  frame_as_hex_string.reserve(frame_length * 2);

  for (uint8_t idx = 0; idx < frame_length; ++idx) {
    frame_as_hex_string.push_back(HEX_NUMBERS[frame[idx] / 16]);
    frame_as_hex_string.push_back(HEX_NUMBERS[frame[idx] % 16]);
  }

  ESP_LOGD(TAG, "Frame toshiba: 0x%s", frame_as_hex_string.substr(0, frame_length * 2).c_str());
  ESP_LOGD(TAG, "  header:      0x%s", frame_as_hex_string.substr(0, 4).c_str());
  ESP_LOGD(TAG, "  data_size:   0x%s", frame_as_hex_string.substr(4, 2).c_str());
  ESP_LOGD(TAG, "  1_checksum:  0x%s", frame_as_hex_string.substr(6, 2).c_str());
  ESP_LOGD(TAG, "  command:     0x%s", frame_as_hex_string.substr(8, 2).c_str());
  ESP_LOGD(TAG, "  data:        0x%s", frame_as_hex_string.substr(10, frame[2] * 2).c_str());
  ESP_LOGD(TAG, "  2_checksum:  0x%s", frame_as_hex_string.substr(frame[2] * 2 + 10, 2).c_str());
}

void ToshibaClimate::setup() {
  climate_ir::ClimateIR::setup();
  if (this->model_ == Model::MODEL_2) {
    this->swing_modes_.insert(this->swing_modes_.begin() + 1, climate::CLIMATE_SWING_BOTH);
    this->swing_modes_.insert(this->swing_modes_.end(), climate::CLIMATE_SWING_HORIZONTAL);
    toshiba_motion_swing_both = 0x01;
    toshiba_motion_swing_vertical = 0x08;
  }
}

void ToshibaClimate::add_default_command_data_(uint8_t* frame) {
  /* Data Length */
  frame[2] = 0x03;

  /* Command */
  frame[4] = TOSHIBA_COMMAND_DEFAULT;

  /* Mode */
  uint8_t mode;
  switch (this->mode) {
    case climate::CLIMATE_MODE_OFF:
      mode = TOSHIBA_MODE_OFF;
      break;

    case climate::CLIMATE_MODE_HEAT:
      mode = TOSHIBA_MODE_HEAT;
      break;

    case climate::CLIMATE_MODE_COOL:
      mode = TOSHIBA_MODE_COOL;
      break;

    case climate::CLIMATE_MODE_FAN_ONLY:
      mode = TOSHIBA_MODE_FAN_ONLY;
      break;

    case climate::CLIMATE_MODE_DRY:
      mode = TOSHIBA_MODE_DRY;
      break;

    case climate::CLIMATE_MODE_AUTO:
    default:
      mode = TOSHIBA_MODE_AUTO;
  }
  frame[6] |= mode;

  /* Temperature */
  uint8_t temperature;
  /* Toshiba remote sets temperature to 22 ̊ C in case of fan only mode */
  if (mode == TOSHIBA_MODE_FAN_ONLY) {
    temperature = 22;
  } else {
    temperature = static_cast<uint8_t>(clamp(this->target_temperature, TOSHIBA_TEMP_MIN, TOSHIBA_TEMP_MAX));
  }
  frame[5] = (temperature - TOSHIBA_TEMP_MIN) << 4;

  /* Fan speed */
  uint8_t fan_mode;
  switch (this->fan_mode) {
    case climate::CLIMATE_FAN_FOCUS:
      fan_mode = TOSHIBA_FAN_SPEED_QUIET;
      break;

    case climate::CLIMATE_FAN_LOW:
      fan_mode = TOSHIBA_FAN_SPEED_1;
      break;

    case climate::CLIMATE_FAN_MIDDLE:
      fan_mode = TOSHIBA_FAN_SPEED_2;
      break;

    case climate::CLIMATE_FAN_MEDIUM:
      fan_mode = TOSHIBA_FAN_SPEED_3;
      break;

    case climate::CLIMATE_FAN_DIFFUSE:
      fan_mode = TOSHIBA_FAN_SPEED_4;
      break;

    case climate::CLIMATE_FAN_HIGH:
      fan_mode = TOSHIBA_FAN_SPEED_5;
      break;

    case climate::CLIMATE_FAN_AUTO:
    default:
      fan_mode = TOSHIBA_FAN_SPEED_AUTO;
  }
  frame[6] |= fan_mode;
}

void ToshibaClimate::add_motion_command_data_(uint8_t* frame) {
  /* Data length */
  frame[2] = 0x01;

  /* Command */
  frame[4] = TOSHIBA_COMMAND_MOTION;

  /* Swing mode */
  uint8_t swing_mode;
  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_BOTH:
      swing_mode = toshiba_motion_swing_both;
      break;

    case climate::CLIMATE_SWING_HORIZONTAL:
      swing_mode = toshiba_motion_swing_horizontal;
      break;

    case climate::CLIMATE_SWING_OFF:
      swing_mode = toshiba_motion_swing_off;
      break;

    case climate::CLIMATE_SWING_VERTICAL:
    default:
      swing_mode = toshiba_motion_swing_vertical;
  }
  frame[5] = swing_mode;
}

void ToshibaClimate::transmit_frame_(uint8_t* frame) {
  uint8_t frame_length = TOSHIBA_FRAME_LENGTH_NO_DATA + frame[2];
  /* Header */
  frame[0] = 0xf2;
  frame[1] = 0x0d;

  /* First checksum */
  frame[3] = frame[0] ^ frame[1] ^ frame[2];

  /* Second checksum starts at pos 4 to (N-1) */
  frame[frame_length - 1] = 0;
  for (uint8_t i = 4; i < frame_length - 1; i++) {
    frame[frame_length - 1] ^= frame[i];
  }
  log_frame(frame);

  /* Transmit */
  auto transmit = this->transmitter_->transmit();
  auto transmit_data = transmit.get_data();
  transmit_data->set_carrier_frequency(38000);

  /* Send the frame twice, like the toshiba remote does */
  for (uint8_t copy = 0; copy < 2; copy++) {
    transmit_data->mark(TOSHIBA_HEADER_MARK);
    transmit_data->space(TOSHIBA_HEADER_SPACE);

    for (uint8_t byte = 0; byte < frame_length; byte++) {
      for (uint8_t bit = 0; bit < 8; bit++) {
        transmit_data->mark(TOSHIBA_BIT_MARK);
        if (frame[byte] & (1 << (7 - bit))) {
          transmit_data->space(TOSHIBA_ONE_SPACE);
        } else {
          transmit_data->space(TOSHIBA_ZERO_SPACE);
        }
      }
    }
    transmit_data->mark(TOSHIBA_BIT_MARK);
    transmit_data->space(TOSHIBA_GAP_SPACE);
  }
  transmit.perform();
}

void ToshibaClimate::transmit_state() {
  uint8_t frame[TOSHIBA_FRAME_MAX_LENGTH];

  if (this->current_mode_ != this->mode || this->current_fan_mode_ != this->fan_mode ||
      this->current_temperature_ != static_cast<uint8_t>(this->target_temperature)) {
    memset(frame, 0, TOSHIBA_FRAME_MAX_LENGTH);
    this->add_default_command_data_(frame);
    this->transmit_frame_(frame);

    /* Toshiba swing mode is vertical when changing from off to any other state */
    if (this->current_mode_ == climate::CLIMATE_MODE_OFF) {
      this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
      this->publish_state();
    }
  }

  if (this->current_swing_mode_ != this->swing_mode) {
    memset(frame, 0, TOSHIBA_FRAME_MAX_LENGTH);
    this->add_motion_command_data_(frame);
    this->transmit_frame_(frame);
  }

  this->current_fan_mode_ = this->fan_mode;
  this->current_swing_mode_ = this->swing_mode;
  this->current_mode_ = this->mode;
  this->current_temperature_ = static_cast<uint8_t>(this->target_temperature);
}

bool ToshibaClimate::on_receive(remote_base::RemoteReceiveData data) {
  uint8_t frame[TOSHIBA_FRAME_MAX_LENGTH] = {0};
  uint8_t frame_length = TOSHIBA_FRAME_LENGTH_NO_DATA;

  /* Validate header */
  if (!data.expect_item(TOSHIBA_HEADER_MARK, TOSHIBA_HEADER_SPACE)) {
    return false;
  }

  /* Decode bytes */
  for (uint8_t byte = 0; byte < frame_length; byte++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (data.expect_item(TOSHIBA_BIT_MARK, TOSHIBA_ONE_SPACE)) {
        frame[byte] |= 1 << (7 - bit);
      } else if (data.expect_item(TOSHIBA_BIT_MARK, TOSHIBA_ZERO_SPACE)) {
        /* Bit is already clear */
      } else {
        return false;
      }
    }

    /* Update length */
    if (byte == 3) {
      /* Validate the first checksum before trusting the length field */
      if ((frame[0] ^ frame[1] ^ frame[2]) != frame[3]) {
        return false;
      }
      frame_length = frame[2] + TOSHIBA_FRAME_LENGTH_NO_DATA;
    }
  }

  /* Validate the second checksum before trusting any more of the frame --
   * starts at pos 4 to (N-1) */
  uint8_t checksum = 0;
  for (uint8_t i = 4; i < frame_length - 1; i++) {
    checksum ^= frame[i];
  }

  if (checksum != frame[frame_length - 1]) {
    return false;
  }

  log_frame(frame);

  /* Command type */
  if (frame[4] == TOSHIBA_COMMAND_MOTION) {
    /* Get the swing mode  */
    uint8_t swing_mode = frame[5] & 0x0f;
    if (swing_mode == toshiba_motion_swing_both)
      this->swing_mode = climate::CLIMATE_SWING_BOTH;
    else if (swing_mode == toshiba_motion_swing_vertical)
      this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
    else if (swing_mode == toshiba_motion_swing_horizontal)
      this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
    else
      this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->current_swing_mode_ = this->swing_mode;
  } else if (frame[4] == TOSHIBA_COMMAND_DEFAULT) {
    /* Get the mode. */
    switch (frame[6] & 0x0f) {
      case TOSHIBA_MODE_OFF:
        this->mode = climate::CLIMATE_MODE_OFF;
        break;

      case TOSHIBA_MODE_HEAT:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;

      case TOSHIBA_MODE_COOL:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;

      case TOSHIBA_MODE_FAN_ONLY:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;

      case TOSHIBA_MODE_DRY:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;

      case TOSHIBA_MODE_AUTO:
      default:
        this->mode = climate::CLIMATE_MODE_AUTO;
    }
    this->current_mode_ = this->mode;

    /* Get the fan mode */
    switch (frame[6] & 0xf0) {
      case TOSHIBA_FAN_SPEED_QUIET:
        this->fan_mode = climate::CLIMATE_FAN_FOCUS;
        break;

      case TOSHIBA_FAN_SPEED_1:
        this->fan_mode = climate::CLIMATE_FAN_LOW;
        break;

      case TOSHIBA_FAN_SPEED_2:
        this->fan_mode = climate::CLIMATE_FAN_MIDDLE;
        break;

      case TOSHIBA_FAN_SPEED_3:
        this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        break;

      case TOSHIBA_FAN_SPEED_4:
        this->fan_mode = climate::CLIMATE_FAN_DIFFUSE;
        break;

      case TOSHIBA_FAN_SPEED_5:
        this->fan_mode = climate::CLIMATE_FAN_HIGH;
        break;

      case TOSHIBA_FAN_SPEED_AUTO:
      default:
        this->fan_mode = climate::CLIMATE_FAN_AUTO;
    }
    this->current_fan_mode_ = this->fan_mode;

    /* Get the target temperature */
    this->target_temperature = (frame[5] >> 4) + TOSHIBA_TEMP_MIN;
  }

  this->publish_state();

  return true;
}

} /* namespace toshiba */
} /* namespace esphome */
