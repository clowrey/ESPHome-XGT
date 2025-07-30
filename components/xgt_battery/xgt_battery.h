#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace xgt_battery {

class XGTBattery : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_battery_voltage_sensor(sensor::Sensor *sensor) { battery_voltage_sensor_ = sensor; }
  void set_battery_temperature_sensor(sensor::Sensor *sensor) { battery_temperature_sensor_ = sensor; }
  void set_battery_charge_sensor(sensor::Sensor *sensor) { battery_charge_sensor_ = sensor; }
  void set_battery_health_sensor(sensor::Sensor *sensor) { battery_health_sensor_ = sensor; }
  void set_min_cell_voltage_sensor(sensor::Sensor *sensor) { min_cell_voltage_sensor_ = sensor; }
  void set_max_cell_voltage_sensor(sensor::Sensor *sensor) { max_cell_voltage_sensor_ = sensor; }
  void set_cell_divergence_sensor(sensor::Sensor *sensor) { cell_divergence_sensor_ = sensor; }

  void set_num_charges_sensor(sensor::Sensor *sensor) { num_charges_sensor_ = sensor; }
  void set_cell_size_sensor(sensor::Sensor *sensor) { cell_size_sensor_ = sensor; }
  void set_parallel_count_sensor(sensor::Sensor *sensor) { parallel_count_sensor_ = sensor; }
  
  // Cell voltage sensors (up to 10 cells)
  void set_cell_voltage_sensor(uint8_t cell, sensor::Sensor *sensor) {
    if (cell < 10) {
      cell_voltage_sensors_[cell] = sensor;
    }
  }

  void set_update_interval(uint32_t update_interval) { update_interval_ = update_interval; }

 protected:
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_temperature_sensor_{nullptr};
  sensor::Sensor *battery_charge_sensor_{nullptr};
  sensor::Sensor *battery_health_sensor_{nullptr};
  sensor::Sensor *min_cell_voltage_sensor_{nullptr};
  sensor::Sensor *max_cell_voltage_sensor_{nullptr};
  sensor::Sensor *cell_divergence_sensor_{nullptr};

  sensor::Sensor *num_charges_sensor_{nullptr};
  sensor::Sensor *cell_size_sensor_{nullptr};
  sensor::Sensor *parallel_count_sensor_{nullptr};
  sensor::Sensor *cell_voltage_sensors_[10]{nullptr};

  uint32_t update_interval_{10000};  // Default 10 seconds
  uint32_t last_update_{0};
  
  // State machine for non-blocking operation
  enum DataState {
    STATE_IDLE,
    STATE_WAKE,
    STATE_NUM_CHARGES,
    STATE_CELL_SIZE,
    STATE_PARALLEL_COUNT,
    STATE_BATTERY_HEALTH,
    STATE_CHARGE,
    STATE_TEMPERATURE,
    STATE_PACK_VOLTAGE,
    STATE_CELL_VOLTAGES,
    STATE_COMPLETE
  };
  
  DataState current_state_{STATE_IDLE};
  uint32_t state_start_time_{0};
  uint8_t current_cell_{0};
  uint8_t command_buffer_[32]{0};
  uint8_t rx_length_{0};
  
  // Battery data storage
  uint16_t batt_health_ = 0;
  uint16_t cell_size_ = 0;
  uint16_t parallel_cnt_ = 0;
  uint16_t charge_ = 0;
  uint16_t num_charges_ = 0;
  float temperature_ = 0;
  float pack_voltage_ = 0;
  float cell_voltages_[10] = {0};


  // Protocol commands
  static const uint8_t num_charges_cmd_[8];
  static const uint8_t cell_size_cmd_[8];
  static const uint8_t parallel_cnt_cmd_[8];
  static const uint8_t batt_health_cmd_[8];
  static const uint8_t charge_cmd_[8];
  static const uint8_t temperature_cmd_[8];
  static const uint8_t pack_voltage_cmd_[8];
  static const uint8_t cell_voltages_cmd_[8];
  static const uint8_t lookup_[16];

  // Protocol methods
  bool check_crc(uint8_t *rx_buf, uint8_t length);
  int8_t send_battery(uint8_t *buf, const uint8_t *command, uint8_t cmd_length, uint8_t *rx_length);
  void publish_sensors();
  void process_current_state();
};

}  // namespace xgt_battery
}  // namespace esphome 