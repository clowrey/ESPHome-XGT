#include "xgt_battery.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <string>
#include <cstring>
#include "driver/uart.h"

namespace esphome {
namespace xgt_battery {

static const char *const TAG = "xgt_battery";

// Protocol commands from the original C++ code

const uint8_t XGTBattery::num_charges_cmd_[8] = {
    0x33, 0xC8, 0x3, 0x0, 0x2A, 0x0, 0x0, 0xCC
};

const uint8_t XGTBattery::cell_size_cmd_[8] = {
    0x33, 0x27, 0xBB, 0x10, 0x0, 0x0, 0x0, 0xCC
};

const uint8_t XGTBattery::parallel_cnt_cmd_[8] = {
    0x33, 0x67, 0xBB, 0x50, 0x0, 0x0, 0x0, 0xCC
};

const uint8_t XGTBattery::batt_health_cmd_[8] = {
    0x33, 0xC4, 0x3, 0x0, 0x26, 0x0, 0x0, 0xCC
};

const uint8_t XGTBattery::charge_cmd_[8] = {
    0x33, 0x13, 0x3, 0x80, 0x10, 0x0, 0x0, 0xCC
};

const uint8_t XGTBattery::temperature_cmd_[8] = {
    0x33, 0x3B, 0x3, 0xC0, 0x58, 0x0, 0x0, 0xCC
};

const uint8_t XGTBattery::pack_voltage_cmd_[8] = {
    0x33, 0x43, 0x3, 0xC0, 0x0, 0x0, 0x0, 0xCC
};

const uint8_t XGTBattery::cell_voltages_cmd_[8] = {
    0x33, 0x23, 0x03, 0xC0, 0x00, 0x0, 0x0, 0xCC
};

// Lookup table to reverse bit order (MSB to LSB conversion)
const uint8_t XGTBattery::lookup_[16] = {
    0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe, 
    0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
};

void XGTBattery::setup() {
    ESP_LOGCONFIG(TAG, "Setting up XGT Battery...");
    
    // DEBUGGING: Test basic UART connectivity
    ESP_LOGV(TAG, "=== UART CONNECTIVITY TEST ===");
    ESP_LOGV(TAG, "Checking if any bytes available during setup...");
    uint8_t setup_available = this->available();
    ESP_LOGV(TAG, "Bytes available during setup: %d", setup_available);
    
    // Test basic UART connectivity using ESPHome methods
    ESP_LOGV(TAG, "Testing UART connectivity with ESPHome methods...");
    uint8_t test_pattern = 0x55;
    this->write(test_pattern);
    this->flush();
    
    delay(50);  // Give time for any echo/response
    uint8_t echo_length = 0;
    uint8_t echo_buffer[10];
    uint32_t start_time = millis();
    
    while (echo_length < 10 && (millis() - start_time) < 100) {  // 100ms timeout
        if (this->available()) {
            echo_buffer[echo_length] = this->read();
            echo_length++;
        } else {
            delay(1);
        }
    }
    
    ESP_LOGV(TAG, "UART connectivity test: received %d bytes", echo_length);
    if (echo_length > 0) {
        ESP_LOGV(TAG, "UART appears to be working - received response!");
        for (int i = 0; i < echo_length; i++) {
            ESP_LOGV(TAG, "Response byte[%d]: 0x%02X", i, echo_buffer[i]);
        }
    } else {
        ESP_LOGV(TAG, "No echo received - this is normal for XGT battery (doesn't echo test bytes)");
    }
    ESP_LOGV(TAG, "=== END UART TEST ===");
}

void XGTBattery::loop() {
    uint32_t now = millis();
    
    // Check if it's time to start a new data collection cycle
    if (current_state_ == STATE_IDLE && now - this->last_update_ > this->update_interval_) {
        current_state_ = STATE_WAKE;
        state_start_time_ = now;
        current_cell_ = 0;
    }
    
    // Process current state
    if (current_state_ != STATE_IDLE) {
        process_current_state();
    }
}

void XGTBattery::dump_config() {
    ESP_LOGCONFIG(TAG, "XGT Battery:");
    ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", this->update_interval_);
    
    // Validate UART settings for XGT battery requirements
    ESP_LOGCONFIG(TAG, "  UART Configuration:");
    ESP_LOGCONFIG(TAG, "    Baud Rate: 9600 (required for XGT battery)");
    ESP_LOGCONFIG(TAG, "    Parity: EVEN (required for XGT battery - 8E1 format)");
    ESP_LOGCONFIG(TAG, "    Signal Inversion: Required for TX/RX pins");
    
    // Check UART settings using the standard ESPHome method
    this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
    
    // Log a warning if user might have missed the critical UART requirements
    ESP_LOGCONFIG(TAG, "  IMPORTANT: XGT battery requires:");
    ESP_LOGCONFIG(TAG, "    - Baud rate: 9600");
    ESP_LOGCONFIG(TAG, "    - Parity: EVEN (8E1 format)"); 
    ESP_LOGCONFIG(TAG, "    - TX/RX pins must be inverted: true");
    ESP_LOGCONFIG(TAG, "    - Example UART config in YAML:");
    ESP_LOGCONFIG(TAG, "      uart:");
    ESP_LOGCONFIG(TAG, "        baud_rate: 9600");
    ESP_LOGCONFIG(TAG, "        parity: EVEN");
    ESP_LOGCONFIG(TAG, "        tx_pin:");
    ESP_LOGCONFIG(TAG, "          number: GPIO43");
    ESP_LOGCONFIG(TAG, "          inverted: true");
    ESP_LOGCONFIG(TAG, "        rx_pin:");
    ESP_LOGCONFIG(TAG, "          number: GPIO44");
    ESP_LOGCONFIG(TAG, "          inverted: true");
}

float XGTBattery::get_setup_priority() const {
    return setup_priority::DATA;
}

bool XGTBattery::check_crc(uint8_t *rx_buf, uint8_t length) {
    uint16_t crc = 0;
    
    // Check message format validity - match working implementation exactly
    if (rx_buf[0] == 0xA5 && rx_buf[1] == 0xA5 || rx_buf[0] == 0xCC && rx_buf[7] == 0x33) {
        if (rx_buf[0] == 0xCC) {
            // Short message type
            crc += rx_buf[0];                        // add first byte (0xCC)
            for (uint8_t i = 2; i < length; i++) {  // loop starting at position 2 to exclude CRC
                crc += rx_buf[i];                    // sum bytes in packet
            }
            crc = crc % 256;  // take modulo 256
            if (crc != rx_buf[1]) {
                return false;  // if calculated CRC does not match byte 1 (crc) return false
            }
        } else {
            // Long message type
            length -= (rx_buf[3] & 0xF);                 // size of data - number of padding bytes
            for (uint8_t i = 2; i < length - 2; i++) {  // exclude A5A5 header and final word (CRC)
                crc += rx_buf[i];
            }
            if ((rx_buf[length - 2] << 8 | rx_buf[length - 1]) != crc) {
                return false;
            }
        }
    } else {
        return false;  // Invalid message format
    }
    return true;  // return true, good CRC
}

int8_t XGTBattery::send_battery(uint8_t *buf, const uint8_t *command, uint8_t cmd_length, uint8_t *rx_length) {
    *rx_length = 0;
    uint8_t attempts = 0;
    
    // Detect command type based on first bytes
    bool is_long_command = (command[0] == 0xA5 && command[1] == 0xA5);
    
    ESP_LOGV(TAG, "Command type: %s (%d bytes)", is_long_command ? "LONG" : "SHORT", cmd_length);
    
    while (*rx_length == 0 && attempts <= 1) {
        if (attempts > 0) {
            delay(50);  // Match working implementation retry delay
        }
        attempts++;
        
        // Debug: Print command being sent - show ALL bytes for model command
        ESP_LOGV(TAG, "Sending command (%d bytes):", cmd_length);
        for (uint8_t i = 0; i < cmd_length; i++) {  // Show ALL bytes, not just first 16
            ESP_LOGV(TAG, "  CMD[%d] = 0x%02X", i, command[i]);
        }
        
        // Use ESPHome native UART methods to avoid driver conflicts
        this->write_array(command, cmd_length);  // Send command
        this->flush();                           // Wait for TX completion and flush buffers
        
        // CRITICAL: Clear input buffer like INO file does with uart_flush()
        // This prevents reading stale data from previous commands
        ESP_LOGV(TAG, "Clearing input buffer...");
        uint8_t cleared = 0;
        while (this->available()) {
            this->read();
            cleared++;
        }
        ESP_LOGV(TAG, "Cleared %d stale bytes from input buffer", cleared);
        
        // Give battery more time for long commands (model command needs longer)
        if (is_long_command) {
            delay(50);  // Model command needs more time
        } else {
            delay(15);  // Normal commands
        }
        
        // Read response using ESPHome method with longer timeout for model command
        uint32_t timeout_ms = is_long_command ? 100 : 25;  // Longer timeout for model command
        uint32_t start_time = millis();
        *rx_length = 0;
        while (*rx_length < 32 && (millis() - start_time) < timeout_ms) {
            if (this->available()) {
                buf[*rx_length] = this->read();
                (*rx_length)++;
                // If we got some data, give a little more time for remaining bytes
                if (*rx_length == 1) {
                    start_time = millis();  // Reset timer on first byte
                }
            } else {
                delay(1);  // Small delay to prevent tight polling
            }
        }
        
        ESP_LOGV(TAG, "ESPHome UART: sent %d bytes, received %d bytes", cmd_length, *rx_length);
    }
    
    if (*rx_length < 8) {
        return 1;  // Need at least 8 bytes minimum
    }
    
    // Debug: Print RAW received data BEFORE processing
    ESP_LOGV(TAG, "Raw received %d bytes:", *rx_length);
    for (uint8_t i = 0; i < *rx_length && i < 32; i++) {
        ESP_LOGV(TAG, "  RAW[%d] = 0x%02X", i, buf[i]);
    }
    
    if (is_long_command) {
        // Long command (model): Process like INO file - NO echo removal, process response directly
        ESP_LOGV(TAG, "Processing as LONG command (model) - processing %d-byte response directly", *rx_length);
        
        // Convert bit order from MSB first to LSB first for the response (like INO file)
        for (uint8_t i = 0; i < *rx_length; ++i) {
            buf[i] = (lookup_[buf[i] & 0b1111] << 4) | lookup_[buf[i] >> 4];
        }
        
    } else {
        // Short command: Handle echo + response like before
        if (*rx_length < 16) {
            return 1;  // Need at least 16 bytes (8 echo + 8 response)
        }
        
        ESP_LOGV(TAG, "Processing as SHORT command - removing 8-byte echo, keeping 8-byte response");
        
        // Single-wire interface: First 8 bytes are command echo, next 8 bytes are response
        // Shift the response to the beginning of the buffer and update length
        for (uint8_t i = 0; i < 8; i++) {
            buf[i] = buf[i + 8];  // Move response bytes to start of buffer
        }
        *rx_length = 8;  // Now we only have the 8-byte response
        
        ESP_LOGV(TAG, "After removing command echo, response is %d bytes:", *rx_length);
        for (uint8_t i = 0; i < *rx_length; i++) {
            ESP_LOGV(TAG, "  RESPONSE[%d] = 0x%02X", i, buf[i]);
        }
        
        // Convert bit order from MSB first to LSB first (only for the response)
        for (uint8_t i = 0; i < *rx_length; ++i) {
            buf[i] = (lookup_[buf[i] & 0b1111] << 4) | lookup_[buf[i] >> 4];
        }
    }
    
    // Debug: Print response data AFTER bit reversal
    ESP_LOGV(TAG, "After bit reversal, response %d bytes:", *rx_length);
    for (uint8_t i = 0; i < *rx_length && i < 16; i++) {
        ESP_LOGV(TAG, "  [%d] = 0x%02X", i, buf[i]);
    }
    
    // Check CRC
    if (!this->check_crc(buf, *rx_length)) {
        ESP_LOGW(TAG, "CRC check failed for %d byte message", *rx_length);
        return -1;  // CRC error
    }
    
    return 0;  // Success
}



void XGTBattery::publish_sensors() {
    if (this->battery_voltage_sensor_ != nullptr) {
        this->battery_voltage_sensor_->publish_state(pack_voltage_);
    }
    
    if (this->battery_temperature_sensor_ != nullptr) {
        this->battery_temperature_sensor_->publish_state(temperature_);
    }
    
    if (this->battery_charge_sensor_ != nullptr) {
        this->battery_charge_sensor_->publish_state(charge_);
    }
    
    if (this->battery_health_sensor_ != nullptr) {
        this->battery_health_sensor_->publish_state(batt_health_);
    }
    

    
    if (this->num_charges_sensor_ != nullptr) {
        this->num_charges_sensor_->publish_state(num_charges_);
    }
    
    if (this->cell_size_sensor_ != nullptr) {
        this->cell_size_sensor_->publish_state(cell_size_);
    }
    
    if (this->parallel_count_sensor_ != nullptr) {
        this->parallel_count_sensor_->publish_state(parallel_cnt_);
    }
    
    // Publish cell voltages
    for (uint8_t i = 0; i < 10; i++) {
        if (this->cell_voltage_sensors_[i] != nullptr) {
            this->cell_voltage_sensors_[i]->publish_state(cell_voltages_[i]);
        }
    }
    
    // Calculate and publish min/max cell voltages and divergence
    float min_voltage = 99.0f;
    float max_voltage = 0.0f;
    bool has_valid_cells = false;
    
    for (uint8_t i = 0; i < 10; i++) {
        if (cell_voltages_[i] > 0.0f) {  // Only consider valid (non-zero) cell voltages
            has_valid_cells = true;
            if (cell_voltages_[i] < min_voltage) {
                min_voltage = cell_voltages_[i];
            }
            if (cell_voltages_[i] > max_voltage) {
                max_voltage = cell_voltages_[i];
            }
        }
    }
    
    if (has_valid_cells) {
        if (this->min_cell_voltage_sensor_ != nullptr) {
            this->min_cell_voltage_sensor_->publish_state(min_voltage);
        }
        
        if (this->max_cell_voltage_sensor_ != nullptr) {
            this->max_cell_voltage_sensor_->publish_state(max_voltage);
        }
        
        if (this->cell_divergence_sensor_ != nullptr) {
            float divergence = max_voltage - min_voltage;
            this->cell_divergence_sensor_->publish_state(divergence);
        }
    }
    
    ESP_LOGD(TAG, "Charge: %d%%, Health: %d%%, Temp: %.1f°C, Voltage: %.2fV, Charges: %d, CellSize: %dmAh, Parallel: %d, Cells: [%.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f]V", 
             charge_, batt_health_, temperature_, pack_voltage_, num_charges_, cell_size_, parallel_cnt_,
             cell_voltages_[0], cell_voltages_[1], cell_voltages_[2], cell_voltages_[3], cell_voltages_[4],
             cell_voltages_[5], cell_voltages_[6], cell_voltages_[7], cell_voltages_[8], cell_voltages_[9]);
}

void XGTBattery::process_current_state() {
    uint32_t now = millis();
    int8_t cmd_error = 0;
    
    switch (current_state_) {
        case STATE_WAKE:
            if (now - state_start_time_ >= 10) {  // Quick transition to wake
                // Send wake byte using ESPHome UART methods
                ESP_LOGV(TAG, "Sending wake byte 0x0 to battery");
                uint8_t wake_byte = 0x0;
                this->write(wake_byte);
                this->flush();
                delay(70);  // 70ms delay like working implementation
                
                current_state_ = STATE_NUM_CHARGES;
                state_start_time_ = now;
            }
            break;
            

            
        case STATE_NUM_CHARGES:
            if (now - state_start_time_ >= 100) {  // Longer delay to prevent response mixing
                cmd_error = this->send_battery(command_buffer_, num_charges_cmd_, sizeof(num_charges_cmd_), &rx_length_);
                ESP_LOGV(TAG, "Num charges command: error=%d, rx_length=%d", cmd_error, rx_length_);
                if (rx_length_ >= 8) {  // Process data regardless of CRC error, like working implementation
                    num_charges_ = __builtin_bswap16((command_buffer_[4] << 8) | command_buffer_[5]);
                    ESP_LOGV(TAG, "Num charges: buffer[4]=%02X, buffer[5]=%02X, result=%d", command_buffer_[4], command_buffer_[5], num_charges_);
                }
                current_state_ = STATE_CELL_SIZE;
                state_start_time_ = now;
            }
            break;
            
        case STATE_CELL_SIZE:
            if (now - state_start_time_ >= 100) {  // Longer delay to prevent response mixing
                cmd_error = this->send_battery(command_buffer_, cell_size_cmd_, sizeof(cell_size_cmd_), &rx_length_);
                if (rx_length_ >= 8) {
                    // Extract raw value like INO file, but apply scaling for mAh units
                    uint16_t raw_cell_size = command_buffer_[5];
                    cell_size_ = raw_cell_size * 100;  // Scale: 40 raw -> 4000mAh for 4Ah battery
                    ESP_LOGV(TAG, "Cell size: buffer[5]=%02X, raw=%d, scaled=%dmAh", command_buffer_[5], raw_cell_size, cell_size_);
                }
                current_state_ = STATE_PARALLEL_COUNT;
                state_start_time_ = now;
            }
            break;
            
        case STATE_PARALLEL_COUNT:
            if (now - state_start_time_ >= 50) {
                cmd_error = this->send_battery(command_buffer_, parallel_cnt_cmd_, sizeof(parallel_cnt_cmd_), &rx_length_);
                if (rx_length_ >= 8) {
                    // Match INO file exactly: use buffer[4] directly
                    parallel_cnt_ = command_buffer_[4];  // Read actual value from battery
                    ESP_LOGV(TAG, "Parallel count: buffer[4]=%02X, result=%d (from battery)", command_buffer_[4], parallel_cnt_);
                }
                current_state_ = STATE_BATTERY_HEALTH;
                state_start_time_ = now;
            }
            break;
            
        case STATE_BATTERY_HEALTH:
            if (now - state_start_time_ >= 50) {
                cmd_error = this->send_battery(command_buffer_, batt_health_cmd_, sizeof(batt_health_cmd_), &rx_length_);
                if (rx_length_ >= 8) {
                    uint16_t health_raw = __builtin_bswap16((command_buffer_[4] << 8) | command_buffer_[5]);
                    // Match INO file exactly: use raw cell size (not scaled) for health calculation
                    uint16_t raw_cell_size = cell_size_ / 100;  // Convert back to raw value (4000 -> 40)
                    if (raw_cell_size > 0 && parallel_cnt_ > 0) {
                        batt_health_ = health_raw / (raw_cell_size * parallel_cnt_);
                        ESP_LOGV(TAG, "Battery health (INO method): raw=%d, raw_cell_size=%d, parallel_cnt=%d, result=%d%%", health_raw, raw_cell_size, parallel_cnt_, batt_health_);
                    } else {
                        // Fallback if values are invalid
                        batt_health_ = (health_raw * 100) / 255;
                        ESP_LOGV(TAG, "Battery health (fallback): raw=%d, result=%d%%", health_raw, batt_health_);
                    }
                }
                current_state_ = STATE_CHARGE;
                state_start_time_ = now;
            }
            break;
            
        case STATE_CHARGE:
            if (now - state_start_time_ >= 50) {
                cmd_error = this->send_battery(command_buffer_, charge_cmd_, sizeof(charge_cmd_), &rx_length_);
                if (rx_length_ >= 8) {
                    uint16_t charge_raw = __builtin_bswap16((command_buffer_[4] << 8) | command_buffer_[5]);
                    charge_ = charge_raw / 255;  // Convert to percentage (0-100) - matches working implementation
                    ESP_LOGV(TAG, "Charge: raw=%d, result=%d%%", charge_raw, charge_);
                }
                current_state_ = STATE_TEMPERATURE;
                state_start_time_ = now;
            }
            break;
            
        case STATE_TEMPERATURE:
            if (now - state_start_time_ >= 50) {
                cmd_error = this->send_battery(command_buffer_, temperature_cmd_, sizeof(temperature_cmd_), &rx_length_);
                if (rx_length_ >= 8) {
                    uint16_t temp_raw = __builtin_bswap16((command_buffer_[4] << 8) | command_buffer_[5]);
                    temperature_ = -30 + ((temp_raw - 2431) / 10);  // Integer division like working implementation
                    ESP_LOGV(TAG, "Temperature: raw=%d, result=%.1f°C", temp_raw, temperature_);
                }
                current_state_ = STATE_PACK_VOLTAGE;
                state_start_time_ = now;
            }
            break;
            
        case STATE_PACK_VOLTAGE:
            if (now - state_start_time_ >= 50) {
                cmd_error = this->send_battery(command_buffer_, pack_voltage_cmd_, sizeof(pack_voltage_cmd_), &rx_length_);
                ESP_LOGV(TAG, "Pack voltage command: error=%d, rx_length=%d", cmd_error, rx_length_);
                if (rx_length_ >= 8) {  // Process data regardless of CRC error
                    uint16_t voltage_raw = __builtin_bswap16((command_buffer_[4] << 8) | command_buffer_[5]);
                    pack_voltage_ = voltage_raw / 1000.0;
                    ESP_LOGV(TAG, "Pack voltage: buffer[4]=%02X, buffer[5]=%02X, raw=%d, result=%.2fV", 
                             command_buffer_[4], command_buffer_[5], voltage_raw, pack_voltage_);
                }
                current_state_ = STATE_CELL_VOLTAGES;
                current_cell_ = 1;  // Start with cell 1
                state_start_time_ = now;
            }
            break;
            
        case STATE_CELL_VOLTAGES:
            if (now - state_start_time_ >= 50) {  // Add delay for cell voltage commands too
                if (current_cell_ <= 10) {
                    uint8_t cell_voltages_cmd_copy[8];
                    memcpy(cell_voltages_cmd_copy, cell_voltages_cmd_, 8);
                    cell_voltages_cmd_copy[4] = (lookup_[current_cell_ * 2 & 0b1111] << 4) | (lookup_[current_cell_ * 2 >> 4]);
                    cell_voltages_cmd_copy[1] = (lookup_[current_cell_ * 2 + 194 & 0b1111] << 4) | (lookup_[current_cell_ * 2 + 194 >> 4]);
                    
                    cmd_error = this->send_battery(command_buffer_, cell_voltages_cmd_copy, 8, &rx_length_);
                    if (rx_length_ >= 8) {
                        uint16_t cell_voltage_raw = __builtin_bswap16((command_buffer_[4] << 8) | command_buffer_[5]);
                        cell_voltages_[current_cell_ - 1] = cell_voltage_raw / 1000.0;
                        ESP_LOGV(TAG, "Cell %d voltage: raw=%d, result=%.3fV", current_cell_, cell_voltage_raw, cell_voltages_[current_cell_ - 1]);
                    }
                    current_cell_++;
                    state_start_time_ = now;  // Reset timer for next cell
                } else {
                    current_state_ = STATE_COMPLETE;
                }
            }
            break;
            
        case STATE_COMPLETE:
            this->publish_sensors();
            current_state_ = STATE_IDLE;
            this->last_update_ = now;
            break;
            
        default:
            current_state_ = STATE_IDLE;
            break;
    }
}

}  // namespace xgt_battery
}  // namespace esphome 