#include "xgt_battery.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <string>
#include <cstring>
#include <climits>
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

// ---------------------------------------------------------------------------------------------
// Register discovery. Every probe is a READ; no write commands (1200/1204/1206/120C/1300/1306,
// lockout or calibration resets) can be built here: long frames always carry the 0003 read tag.
//
// Sources for what is known (see README "Register discovery"):
//   C0 0x0054 cycles, 0x0064 remaining mAh, 0x0108 charge, 0x0300 pack mV, 0x0302-0x0314 cells,
//   0x031A/0x031C temperatures (dK), 0x0060 lockout flag, 0x00C0-C4 temperature histogram,
//   0x00D8-DC current histogram; DD 04/08/0A pack info. Makita's tester also reads 0x0056,
//   0x005A-0x005E, 0x006A, 0x0200 and DD 04 (decoded from the XGT Data sigrok capture).
//   Long reads: 1201/1203/1205/120D (charger, w4 00CC), 1302/1304/1307 (tool, w4 000C),
//   1813/1825 (tester, w4 00CB).
// ---------------------------------------------------------------------------------------------

// Addresses the fixed state machine already reads every cycle.
static const uint16_t KNOWN_ADDRS[] = {0x0054, 0x0064, 0x0108, 0x0300, 0x0302, 0x0304, 0x0306, 0x0308,
                                       0x030A, 0x030C, 0x030E, 0x0310, 0x0312, 0x0314, 0x031A};
// Short registers read every cycle: the gap after the cells, the second temperature and its
// neighbours, and the tester's unexplained reads. Live current is most likely among these.
static const uint16_t HOT_C0[] = {0x0316, 0x0318, 0x031C, 0x031E, 0x0320, 0x0322, 0x0324, 0x0326, 0x0328,
                                  0x0200, 0x0202, 0x0056, 0x0060, 0x006A, 0x00D8, 0x00DA, 0x00DC};
// Short registers swept round-robin.
static const uint16_t SWEEP_C0_RANGES[][2] = {
    {0x0000, 0x00FE}, {0x0100, 0x013E}, {0x0200, 0x023E}, {0x0300, 0x033E}, {0x0400, 0x041E}};
// Long single-parameter sweeps: {first param, last param, command, w4}. The command/w4 pairs are
// ones seen reading that parameter family in captures.
static const uint16_t SWEEP_LONG_RANGES[][4] = {
    {0x1200, 0x121F, 0x1201, 0x00CC},  // charger family (0x120A = charge counter?)
    {0x1300, 0x131F, 0x1304, 0x000C},  // tool family
    {0x1800, 0x182F, 0x1825, 0x00CB},  // tester family
    {0x2100, 0x210F, 0x1201, 0x00CC},  // values a charger writes (model, session id)
    {0x3100, 0x310F, 0x1304, 0x000C},  // values a tool writes (tool model)
};

uint8_t XGTBattery::reverse_bits_(uint8_t b) { return (lookup_[b & 0x0F] << 4) | lookup_[b >> 4]; }

// Short frame, logical (LSB-first) bytes: CC crc cmd a b 00 00 33,
// crc = sum of the other seven bytes mod 256; every byte is bit-reversed on the wire.
// Matches the fixed commands above, e.g. C0 0x0300 -> 33 43 03 C0 00 00 00 CC.
void XGTBattery::make_short_cmd_(uint8_t cmd, uint8_t a, uint8_t b, uint8_t *out) {
    uint8_t l[8] = {0xCC, 0x00, cmd, a, b, 0x00, 0x00, 0x33};
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (i != 1) sum += l[i];
    }
    l[1] = sum;
    for (uint8_t i = 0; i < 8; i++) out[i] = reverse_bits_(l[i]);
}

bool XGTBattery::read_register_(uint16_t address, int16_t *value) {
    uint8_t cmd[8];
    this->make_short_cmd_(0xC0, address >> 8, address & 0xFF, cmd);
    int8_t err = this->send_battery(command_buffer_, cmd, 8, &rx_length_);
    if (err != 0 || rx_length_ < 8 || command_buffer_[2] != 0xC0) return false;
    // Same decoding as pack voltage / cells: little-endian word in bytes 4-5.
    *value = (int16_t) ((command_buffer_[5] << 8) | command_buffer_[4]);
    return true;
}

// Value sizes of long-frame parameters (replies don't carry them). From
// paulofloresjunior/makita-battery-diagnostics-web xgt-catalog.js.
int XGTBattery::long_param_len_(uint16_t id) {
    switch (id) {
        case 0x1202: case 0x130B: return 8;
        case 0x1203: case 0x120A: case 0x120B: case 0x130E: return 4;
        case 0x130C: return 1;
        case 0x130F: return 46;
        default:
            if ((id >= 0x1201 && id <= 0x1210) || (id >= 0x1301 && id <= 0x1311)) return 2;
            return -1;
    }
}

// Finds a parameter in a long read reply block ("0001 0000" then id/value pairs, values big-endian).
bool XGTBattery::find_long_param_(const std::vector<uint8_t> &val, uint16_t id, uint32_t *out) {
    size_t o = 4;
    while (o + 2 <= val.size()) {
        uint16_t pid = (val[o] << 8) | val[o + 1];
        int len = long_param_len_(pid);
        if (len < 0 || o + 2 + len > val.size()) return false;
        if (pid == id) {
            uint32_t v = 0;
            for (int k = 0; k < len && k < 4; k++) v = (v << 8) | val[o + 2 + k];
            *out = v;
            return true;
        }
        o += 2 + len;
    }
    return false;
}

// Long read: A5A5 | len | msgid | 4D4C | w4 | cmd | plen | 0003 count ids... | checksum | FF pad.
// Checksum = 16-bit sum of every byte after A5A5 up to it. Bytes are bit-reversed on the wire.
bool XGTBattery::read_long_(uint16_t cmd, uint16_t w4, const std::vector<uint16_t> &params,
                            std::vector<uint8_t> &out) {
    out.clear();
    const size_t n = params.size();
    const size_t plen = 4 + 2 * n;
    const size_t used = 14 + plen + 2;
    const size_t total = (used + 15) / 16 * 16;
    uint8_t f[96];
    if (n == 0 || total > sizeof(f) || total - 16 > 0xF0) return false;
    memset(f, 0xFF, total);
    msg_id_ = (msg_id_ + 1) & 0x0FFF;
    const uint16_t id = 0x5000 | msg_id_;  // bit 14 = request, bit 12 always set
    size_t i = 0;
    f[i++] = 0xA5; f[i++] = 0xA5;
    f[i++] = 0x00; f[i++] = (uint8_t) ((total - 16) | (total - used));
    f[i++] = id >> 8; f[i++] = id & 0xFF;
    f[i++] = 0x4D; f[i++] = 0x4C;
    f[i++] = w4 >> 8; f[i++] = w4 & 0xFF;
    f[i++] = cmd >> 8; f[i++] = cmd & 0xFF;
    f[i++] = plen >> 8; f[i++] = plen & 0xFF;
    f[i++] = 0x00; f[i++] = 0x03;  // read request tag
    f[i++] = n >> 8; f[i++] = n & 0xFF;
    for (uint16_t p : params) { f[i++] = p >> 8; f[i++] = p & 0xFF; }
    uint16_t sum = 0;
    for (size_t k = 2; k < i; k++) sum += f[k];
    f[i++] = sum >> 8; f[i++] = sum & 0xFF;
    for (size_t k = 0; k < total; k++) f[k] = reverse_bits_(f[k]);

    this->write_array(f, total);
    this->flush();
    while (this->available()) this->read();  // drop echo / stale bytes

    // Reply: wait up to 200 ms for the first byte, then read until the length word's total,
    // giving up after a 40 ms gap.
    size_t got = 0, want = sizeof(long_buf_);
    uint32_t start = millis(), last = start;
    while (got < want) {
        uint32_t now = millis();
        if (got == 0 ? now - start > 200 : now - last > 40) break;
        if (!this->available()) { delay(1); continue; }
        long_buf_[got] = reverse_bits_(this->read());
        got++;
        last = millis();
        if (got == 4) {
            if (long_buf_[0] != 0xA5 || long_buf_[1] != 0xA5) return false;
            want = 16 + (long_buf_[3] & 0xF0);
            if (want > sizeof(long_buf_)) return false;
        }
    }
    if (got < 16 || got < want - (long_buf_[3] & 0x0F)) return false;
    const uint8_t *r = long_buf_;
    if (r[6] != 0x4D || r[7] != 0x4C) return false;
    if (((r[4] << 8) | r[5]) != (id ^ 0xC000)) return false;  // same message id, response bit
    const size_t rplen = (r[12] << 8) | r[13];
    const size_t cs_at = 14 + rplen;
    if (cs_at + 2 > got) return false;
    uint16_t rsum = 0;
    for (size_t k = 2; k < cs_at; k++) rsum += r[k];
    if (rsum != ((r[cs_at] << 8) | r[cs_at + 1])) return false;
    out.assign(r + 14, r + cs_at);
    return true;
}

bool XGTBattery::run_probe_(Probe &p) {
    std::vector<uint8_t> v;
    bool ok = false;
    if (p.kind == PROBE_C0) {
        int16_t x;
        ok = this->read_register_(p.addr, &x);
        if (ok) v = {(uint8_t) (x & 0xFF), (uint8_t) (x >> 8)};
    } else if (p.kind == PROBE_DD) {
        uint8_t cmd[8];
        this->make_short_cmd_(0xDD, (uint8_t) p.addr, 0x00, cmd);
        int8_t err = this->send_battery(command_buffer_, cmd, 8, &rx_length_);
        ok = err == 0 && rx_length_ >= 8 && command_buffer_[2] == 0xDD;
        if (ok) v = {command_buffer_[4], command_buffer_[5], command_buffer_[6]};
    } else {
        ok = this->read_long_(p.addr, p.w4, p.params, v);
    }
    if (p.ok) {
        p.prev = p.val;
        p.had_prev = true;
    }
    p.ok = ok;
    p.val = ok ? v : std::vector<uint8_t>{};
    return ok;
}

std::string XGTBattery::format_probe_(const Probe &p, bool mark) const {
    char buf[32];
    std::string s;
    if (p.kind == PROBE_C0) {
        snprintf(buf, sizeof(buf), "%04X=", p.addr);
    } else if (p.kind == PROBE_DD) {
        snprintf(buf, sizeof(buf), "DD%02X=", p.addr);
    } else if (p.params.size() == 1) {
        snprintf(buf, sizeof(buf), "P%04X=", p.params[0]);
    } else {
        snprintf(buf, sizeof(buf), "L%04X/%04X:", p.addr, p.w4);
    }
    s = buf;
    if (!p.ok) return s + "?";
    const bool changed = mark && p.had_prev && p.prev != p.val;
    if (p.kind == PROBE_C0) {
        int16_t x = (int16_t) (p.val[0] | (p.val[1] << 8));
        snprintf(buf, sizeof(buf), "%04X(%d)", (uint16_t) x, x);
        s += buf;
    } else if (p.kind == PROBE_DD) {
        snprintf(buf, sizeof(buf), "%02X %02X %02X", p.val[0], p.val[1], p.val[2]);
        s += buf;
    } else if (p.params.size() == 1) {
        // single parameter: everything after "0001 0000 <id>"
        for (size_t k = p.val.size() >= 6 ? 6 : 0; k < p.val.size(); k++) {
            snprintf(buf, sizeof(buf), "%02X", p.val[k]);
            s += buf;
        }
        if (p.val.size() < 6) s += "(short)";
    } else {
        // multi-parameter: " id=value" pairs, unknown sizes dumped raw
        size_t o = 4;
        while (o + 2 <= p.val.size()) {
            uint16_t pid = (p.val[o] << 8) | p.val[o + 1];
            int len = long_param_len_(pid);
            if (len < 0 || o + 2 + len > p.val.size()) break;
            snprintf(buf, sizeof(buf), " %04X=", pid);
            s += buf;
            for (int k = 0; k < len; k++) {
                snprintf(buf, sizeof(buf), "%02X", p.val[o + 2 + k]);
                s += buf;
            }
            // mark the individual changed value
            if (mark && p.had_prev && o + 2 + len <= p.prev.size() &&
                memcmp(&p.val[o + 2], &p.prev[o + 2], len) != 0)
                s += "*";
            o += 2 + len;
        }
        if (o < p.val.size()) {
            s += " raw:";
            for (; o < p.val.size(); o++) {
                snprintf(buf, sizeof(buf), "%02X", p.val[o]);
                s += buf;
            }
        }
        return s;
    }
    if (changed) s += "*";
    return s;
}

void XGTBattery::log_probes_(const char *tag, std::vector<Probe> &list, size_t from, size_t to) {
    std::string line;
    int on_line = 0;
    for (size_t i = from; i < to && i < list.size(); i++) {
        const Probe &p = list[i];
        std::string item = this->format_probe_(p, true);
        // long probes get their own line; short ones 6 per line
        bool own = p.kind == PROBE_LONG;
        if (own || on_line == 6) {
            if (!line.empty()) ESP_LOGI(TAG, "%s %s", tag, line.c_str());
            line.clear();
            on_line = 0;
        }
        if (own) {
            ESP_LOGI(TAG, "%s %s", tag, item.c_str());
            continue;
        }
        line += item + "  ";
        on_line++;
    }
    if (!line.empty()) ESP_LOGI(TAG, "%s %s", tag, line.c_str());
}

void XGTBattery::build_probes_() {
    auto known = [](uint16_t a) {
        for (uint16_t k : KNOWN_ADDRS)
            if (k == a) return true;
        return false;
    };
    auto in_hot = [this](ProbeKind kind, uint16_t a) {
        for (auto &p : hot_)
            if (p.kind == kind && p.addr == a) return true;
        return false;
    };
    auto add_c0 = [&](std::vector<Probe> &list, uint16_t a) {
        if (known(a) || in_hot(PROBE_C0, a)) return;
        for (auto &p : list)
            if (p.kind == PROBE_C0 && p.addr == a) return;
        Probe p;
        p.kind = PROBE_C0;
        p.addr = a;
        p.w4 = 0;
        list.push_back(p);
    };
    auto add_long = [](std::vector<Probe> &list, uint16_t cmd, uint16_t w4, std::vector<uint16_t> params) {
        Probe p;
        p.kind = PROBE_LONG;
        p.addr = cmd;
        p.w4 = w4;
        p.params = std::move(params);
        list.push_back(p);
    };

    // Hot: configured register sensors, the short list, then the long reads tools and chargers make.
    for (auto &rs : register_sensors_) add_c0(hot_, rs.address);
    for (uint16_t a : HOT_C0) add_c0(hot_, a);
    // Exactly as captured (Malvineous README, issue #4 logs):
    add_long(hot_, 0x1201, 0x00CC, {0x1201, 0x1202, 0x1203, 0x1205, 0x1206, 0x1207, 0x1208, 0x1209, 0x120A, 0x120B});
    add_long(hot_, 0x1203, 0x00CC, {0x1204, 0x120C, 0x120D, 0x120E, 0x1210});
    add_long(hot_, 0x1205, 0x00CC, {0x1201, 0x120D, 0x120F});
    add_long(hot_, 0x1304, 0x000C, {0x1302, 0x1303, 0x1304, 0x1308, 0x1309, 0x130A, 0x1311});
    add_long(hot_, 0x1302, 0x000C, {0x1301, 0x1306, 0x1307, 0x130C, 0x130D, 0x130E, 0x130F, 0x1310});

    if (!scan_enabled_) return;
    for (auto &r : SWEEP_C0_RANGES)
        for (uint32_t a = r[0]; a <= r[1]; a += 2) add_c0(sweep_, a);
    for (uint16_t arg = 0x00; arg <= 0x1F; arg++) {
        Probe p;
        p.kind = PROBE_DD;
        p.addr = arg;
        p.w4 = 0;
        sweep_.push_back(p);
    }
    add_long(sweep_, 0x1307, 0x000C, {0x130B});  // battery model, as tools ask for it
    for (auto &r : SWEEP_LONG_RANGES)
        for (uint32_t id = r[0]; id <= r[1]; id++) add_long(sweep_, r[2], r[3], {(uint16_t) id});
}

// Charge counter estimate from the 1201 read (param 0x120A, mA*s) and capacity (0x1203, mA*s).
void XGTBattery::update_counter_() {
    const Probe *p1201 = nullptr, *p1304 = nullptr;
    for (auto &p : hot_) {
        if (p.kind != PROBE_LONG || !p.ok) continue;
        if (p.addr == 0x1201) p1201 = &p;
        if (p.addr == 0x1304) p1304 = &p;
    }
    uint32_t v;
    if (p1304 != nullptr && pack_soc_sensor_ != nullptr && find_long_param_(p1304->val, 0x1304, &v))
        pack_soc_sensor_->publish_state(v / 256.0f);
    if (p1201 == nullptr) return;
    if (find_long_param_(p1201->val, 0x1203, &v) && counter_capacity_sensor_ != nullptr)
        counter_capacity_sensor_->publish_state(v / 3600.0f);
    uint32_t q;
    if (!find_long_param_(p1201->val, 0x120A, &q)) return;
    const uint32_t now = millis();
    if (counter_charge_sensor_ != nullptr) counter_charge_sensor_->publish_state(q / 3600.0f);
    if (have_q_) {
        const float dt = (now - q_ms_) / 1000.0f;
        if (dt > 0.5f && dt < 120.0f) {
            // mA*s per s = mA; positive = discharging
            const float amps = ((int64_t) q_mas_ - (int64_t) q) / dt / 1000.0f;
            const float watts = amps * pack_voltage_;
            if (watts > 0) energy_wh_ += watts * dt / 3600.0f;
            if (counter_current_sensor_ != nullptr) counter_current_sensor_->publish_state(amps);
            if (counter_power_sensor_ != nullptr) counter_power_sensor_->publish_state(watts);
            if (counter_energy_sensor_ != nullptr) counter_energy_sensor_->publish_state(energy_wh_);
            ESP_LOGI(TAG, "ctr Q=%.1fmAh dQ=%ldmAs dt=%.2fs I=%.2fA P=%.0fW E=%.2fWh", q / 3600.0f,
                     (long) ((int64_t) q_mas_ - (int64_t) q), dt, amps, watts, energy_wh_);
        }
    }
    q_mas_ = q;
    q_ms_ = now;
    have_q_ = true;
}

void XGTBattery::setup() {
    ESP_LOGCONFIG(TAG, "Setting up XGT Battery...");

    this->build_probes_();
    ESP_LOGCONFIG(TAG, "  Discovery: %u probes every cycle, %u in the sweep (%u per cycle)",
                  (unsigned) hot_.size(), (unsigned) sweep_.size(), (unsigned) sweep_per_cycle_);
    
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
    ESP_LOGCONFIG(TAG, "    Half Duplex: Recommended for simplified wiring");
    ESP_LOGCONFIG(TAG, "    Signal Inversion: Required for TX pin");
    
    // Check UART settings using the standard ESPHome method
    this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
    
    // Log recommended configuration for users
    ESP_LOGCONFIG(TAG, "  RECOMMENDED: Half duplex configuration:");
    ESP_LOGCONFIG(TAG, "    - Baud rate: 9600");
    ESP_LOGCONFIG(TAG, "    - Parity: EVEN (8E1 format)"); 
    ESP_LOGCONFIG(TAG, "    - Half duplex: true (single wire)");
    ESP_LOGCONFIG(TAG, "    - TX pin inverted: true");
    ESP_LOGCONFIG(TAG, "    - Example half duplex UART config:");
    ESP_LOGCONFIG(TAG, "      uart:");
    ESP_LOGCONFIG(TAG, "        baud_rate: 9600");
    ESP_LOGCONFIG(TAG, "        half_duplex: true");
    ESP_LOGCONFIG(TAG, "        parity: EVEN");
    ESP_LOGCONFIG(TAG, "        tx_pin:");
    ESP_LOGCONFIG(TAG, "          number: GPIO14");
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
        // Short command: HALF DUPLEX MODE - no echo removal needed
        // All received bytes are the actual response data
        ESP_LOGV(TAG, "Processing as SHORT command in HALF DUPLEX mode - all %d bytes are response data", *rx_length);
        
        // Convert bit order from MSB first to LSB first (all received data is response)
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
                    current_state_ = (hot_.empty() && sweep_.empty()) ? STATE_COMPLETE : STATE_SCAN;
                    hot_index_ = 0;
                    sweep_done_this_cycle_ = 0;
                    state_start_time_ = now;
                }
            }
            break;

        case STATE_SCAN:
            // One probe per tick: all hot probes, then sweep_per_cycle_ sweep probes.
            if (now - state_start_time_ >= 50) {
                if (hot_index_ < hot_.size()) {
                    this->run_probe_(hot_[hot_index_]);
                    hot_index_++;
                    if (hot_index_ == hot_.size()) {
                        this->log_probes_("hot", hot_, 0, hot_.size());
                        ESP_LOGI(TAG, "hot --- pack %.2fV ---", pack_voltage_);
                        this->update_counter_();
                    }
                } else if (!sweep_.empty() && sweep_done_this_cycle_ < sweep_per_cycle_) {
                    this->run_probe_(sweep_[sweep_pos_]);
                    sweep_pos_++;
                    sweep_done_this_cycle_++;
                    if (sweep_pos_ >= sweep_.size()) {
                        sweep_count_++;
                        ESP_LOGI(TAG, "sweep #%u complete (%u probes, * = changed since sweep #%u):",
                                 (unsigned) sweep_count_, (unsigned) sweep_.size(), (unsigned) sweep_count_ - 1);
                        this->log_probes_("sweep", sweep_, 0, sweep_.size());
                        std::string changed;
                        for (auto &p : sweep_)
                            if (p.ok && p.had_prev && p.prev != p.val) {
                                std::string f = this->format_probe_(p, false);
                                changed += f.substr(0, f.find_first_of("=:")) + " ";
                            }
                        ESP_LOGI(TAG, "sweep changed: %s", changed.empty() ? "(none)" : changed.c_str());
                        sweep_pos_ = 0;
                    }
                } else {
                    current_state_ = STATE_COMPLETE;
                }
                state_start_time_ = now;
            }
            break;

        case STATE_COMPLETE:
            for (auto &rs : register_sensors_) {
                for (auto &p : hot_) {
                    if (p.kind == PROBE_C0 && p.addr == rs.address && p.ok) {
                        rs.sensor->publish_state((int16_t) (p.val[0] | (p.val[1] << 8)));
                    }
                }
            }
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