#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <string>
#include <vector>

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

  // Unmapped-register discovery: publish one raw register (signed 16-bit) as a sensor.
  // Its address is read every cycle.
  void add_register_sensor(uint16_t address, sensor::Sensor *sensor) {
    register_sensors_.push_back({address, sensor});
  }
  void set_scan_enabled(bool enabled) { scan_enabled_ = enabled; }
  void set_sweep_per_cycle(uint16_t n) { sweep_per_cycle_ = n; }

  // Estimates from long-frame parameter 0x120A, which looks like the pack's charge counter in mA*s
  // (Malvineous' capture: 0x1203 = 14,400,000 = 4000 mAh * 3600). Unverified.
  void set_counter_charge_sensor(sensor::Sensor *s) { counter_charge_sensor_ = s; }
  void set_counter_capacity_sensor(sensor::Sensor *s) { counter_capacity_sensor_ = s; }
  void set_counter_current_sensor(sensor::Sensor *s) { counter_current_sensor_ = s; }
  void set_counter_power_sensor(sensor::Sensor *s) { counter_power_sensor_ = s; }
  void set_counter_energy_sensor(sensor::Sensor *s) { counter_energy_sensor_ = s; }
  // Long-frame parameter 0x1304, polled by tools while running: state of charge in 1/256 %.
  void set_pack_soc_sensor(sensor::Sensor *s) { pack_soc_sensor_ = s; }

 protected:
  struct RegisterSensor {
    uint16_t address;
    sensor::Sensor *sensor;
  };
  // One read-only probe of the battery: a short C0 register read, a short DD read, or a long
  // (A5A5) read-parameters request. val holds the reply data bytes (short: bytes 4..6,
  // long: the parameter block after "0001 0000").
  enum ProbeKind : uint8_t { PROBE_C0, PROBE_DD, PROBE_LONG };
  struct Probe {
    ProbeKind kind;
    uint16_t addr;  // C0 address, DD argument, or long command word
    uint16_t w4;    // long only: 0x000C tool, 0x00CC charger, 0x00CB tester
    std::vector<uint16_t> params;
    std::vector<uint8_t> val;
    std::vector<uint8_t> prev;
    bool ok{false};
    bool had_prev{false};
  };
  std::vector<RegisterSensor> register_sensors_;
  bool scan_enabled_{true};
  uint16_t sweep_per_cycle_{24};
  std::vector<Probe> hot_;    // read every cycle
  std::vector<Probe> sweep_;  // read sweep_per_cycle_ per cycle, round-robin
  size_t hot_index_{0};
  size_t sweep_pos_{0};
  uint16_t sweep_done_this_cycle_{0};
  uint32_t sweep_count_{0};
  uint16_t msg_id_{0};
  uint8_t long_buf_[288];

  sensor::Sensor *counter_charge_sensor_{nullptr};
  sensor::Sensor *counter_capacity_sensor_{nullptr};
  sensor::Sensor *counter_current_sensor_{nullptr};
  sensor::Sensor *counter_power_sensor_{nullptr};
  sensor::Sensor *counter_energy_sensor_{nullptr};
  sensor::Sensor *pack_soc_sensor_{nullptr};
  bool have_q_{false};
  uint32_t q_mas_{0};
  uint32_t q_ms_{0};
  float energy_wh_{0};

  static uint8_t reverse_bits_(uint8_t b);
  void make_short_cmd_(uint8_t cmd, uint8_t a, uint8_t b, uint8_t *out);
  bool read_register_(uint16_t address, int16_t *value);
  bool run_probe_(Probe &p);
  bool read_long_(uint16_t cmd, uint16_t w4, const std::vector<uint16_t> &params, std::vector<uint8_t> &out);
  static int long_param_len_(uint16_t id);
  static bool find_long_param_(const std::vector<uint8_t> &val, uint16_t id, uint32_t *out);
  std::string format_probe_(const Probe &p, bool mark_changes) const;
  void log_probes_(const char *tag, std::vector<Probe> &list, size_t from, size_t to);
  void build_probes_();
  void update_counter_();

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
    STATE_SCAN,
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