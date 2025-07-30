# XGT Battery Component for ESPHome

This component provides comprehensive integration with Makita XGT battery packs through UART communication, featuring real-time monitoring and optional display integration.

![XGT Battery Display](img/display.jpg)
*Real-time battery monitoring display showing voltage, charge percentage, temperature, and cell diagnostics*

## Features

- Real-time battery monitoring (voltage, temperature, charge level, health)
- Individual cell voltage monitoring (up to 10 cells)
- Advanced cell diagnostics (min/max cell voltages, cell divergence)
- Battery diagnostics (charge cycles, cell capacity, parallel cell count)
- Battery model identification
- Non-blocking state machine implementation
- LVGL display integration support for real-time visualization
- Hardware button support for user interaction

## Hardware Compatibility

### Supported Platforms:
- **ESP32** (all variants including ESP32-S3)
- **Tested Configuration**: Lolin S3 Mini Pro board
- **Display Support**: 128x128 ILI9xxx displays (ST7789V)
- **Additional Hardware**: WS2812 LED support, physical buttons

### Hardware Requirements:
- ESP32 microcontroller
- Direct connection to XGT battery communication pins
- Proper voltage level matching (3.3V logic levels)
- Optional: SPI display for real-time monitoring interface

![Wiring Example](img/wiring-example.jpg)
*Example wiring configuration showing ESP32 connections to XGT battery communication interface*

### Interface Circuit Schematic

The XGT battery communication requires a specific interface circuit to handle the single-wire, bidirectional communication:

```mermaid
flowchart LR
    %% ESP32 Pins
    TX["`**GPIO43**
    (TX - inverted)`"]
    RX["`**GPIO44** 
    (RX - inverted)`"]
    
    %% Interface Components
    R1{"`**R1**
    ┌─[==]─┐
    **10kΩ**`"}
    
    R2{"`**R2**
    ┌─[==]─┐
    **4.7kΩ**`"}
    
    R3{"`**R3**
    ┌─[==]─┐
    **470Ω**`"}
    
    D1{{"`**D1** ──|▷|── **1N60P** (Schottky)`"}}
    
    %% Connection Points
    MID(("`**●**
    Junction`"))
    
    %% XGT Battery
    DATA["`**XGT Data**
    (Pin)`"]
    
    %% Ground Symbols
    GND_ESP["`**⏚**
    **ESP32 GND**`"]
    GND_XGT["`**⏚**
    **XGT GND**`"]
    
    %% Connections with better spacing
    TX -.-> D1
    D1 -.-> MID
    RX -.-> R2
    R2 -.-> MID
    RX -.-> R1
    R1 -.-> GND_ESP
    MID -.-> R3
    R3 -.-> DATA
    GND_ESP -.-> GND_XGT
    
    %% Styling
    classDef pinStyle fill:#e3f2fd,stroke:#1976d2,stroke-width:2px,color:#000
    classDef resistorStyle fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000
    classDef diodeStyle fill:#fce4ec,stroke:#c2185b,stroke-width:2px,color:#000
    classDef junctionStyle fill:#000,stroke:#000,stroke-width:3px,color:#fff
    classDef groundStyle fill:#f1f8e9,stroke:#689f38,stroke-width:2px,color:#000
    classDef dataStyle fill:#e8f5e8,stroke:#4caf50,stroke-width:2px,color:#000
    
    class TX,RX pinStyle
    class R1,R2,R3 resistorStyle
    class D1 diodeStyle
    class MID junctionStyle
    class GND_ESP,GND_XGT groundStyle
    class DATA dataStyle
```

**Component Values:**
- **R1**: 10kΩ (pulldown resistor for RX)
- **R2**: 4.7kΩ (RX to midpoint)
- **R3**: 470Ω (midpoint to XGT data line)
- **D1**: 1N60P Schottky diode (low forward voltage drop)

**Circuit Operation:**
- The 10kΩ pulldown ensures RX reads low when no signal is present
- The 4.7kΩ resistor provides current limiting and voltage division
- The 1N60P Schottky diode allows TX to drive the line high while preventing backfeed (low forward voltage drop improves signal integrity)
- The 470Ω resistor provides current limiting to the XGT battery data line
- Both TX and RX pins must be configured as inverted in software

![Protocol Scope Trace](img/scope.png)
*Oscilloscope trace showing XGT battery communication protocol with proper signal inversion and timing*

## Critical UART Configuration Requirements

⚠️ **IMPORTANT**: The XGT battery requires specific UART settings to function properly:

### Required UART Settings:
- **Baud Rate**: 9600
- **Parity**: EVEN (8E1 format) - **CRITICAL FOR PROPER COMMUNICATION**
- **Data Bits**: 8
- **Stop Bits**: 1
- **Signal Inversion**: Both TX and RX pins must be inverted

### Example UART Configuration:

```yaml
uart:
  - id: xgt_uart
    baud_rate: 9600
    parity: EVEN          # CRITICAL: Must be EVEN parity (8E1)
    tx_pin: 
      number: GPIO43
      inverted: true      # CRITICAL: Must be inverted
    rx_pin: 
      number: GPIO44  
      inverted: true      # CRITICAL: Must be inverted
```

**Failure to set parity to EVEN will result in runtime warnings and communication failures.**

## Available Sensors

### Main Battery Sensors:
- **battery_voltage**: Pack voltage (V)
- **battery_temperature**: Battery temperature (°C)
- **battery_charge**: State of charge percentage (%)
- **battery_health**: State of health percentage (%)

### Advanced Cell Monitoring:
- **min_cell_voltage**: Lowest cell voltage across all cells (V)
- **max_cell_voltage**: Highest cell voltage across all cells (V)
- **cell_divergence**: Voltage difference between min/max cells (V)
- **cell_voltage**: Individual cell voltages (cell_1 through cell_10)

### Diagnostic Sensors:
- **num_charges**: Total battery charge cycles
- **cell_size**: Individual cell capacity (mAh)
- **parallel_count**: Number of parallel cell groups

## Usage

### Basic Configuration:

```yaml
# External Components
external_components:
  - source: components
    components: [xgt_battery]

# UART Configuration (with critical settings)
uart:
  - id: xgt_uart
    baud_rate: 9600
    parity: EVEN          # CRITICAL: XGT battery requires EVEN parity
    tx_pin: 
      number: GPIO43
      inverted: true      # CRITICAL: XGT battery requires inverted signals
    rx_pin: 
      number: GPIO44  
      inverted: true      # CRITICAL: XGT battery requires inverted signals

# XGT Battery Component
xgt_battery:
  id: xgt_batt
  uart_id: xgt_uart
  update_interval: 5s
  
  # Main battery sensors
  battery_voltage:
    name: "Battery Voltage"
    id: battery_voltage
  battery_temperature:
    name: "Battery Temperature"
    id: battery_temperature
  battery_charge:
    name: "Battery Charge"
    id: battery_charge
  battery_health:
    name: "Battery Health"
    id: battery_health
    
  # Advanced cell monitoring
  min_cell_voltage:
    name: "Min Cell Voltage"
    id: min_cell_voltage
  max_cell_voltage:
    name: "Max Cell Voltage"
    id: max_cell_voltage
  cell_divergence:
    name: "Cell Divergence"
    id: cell_divergence
    
  # Diagnostic sensors
  num_charges:
    name: "Battery Charge Cycles"
    id: battery_cycles
  cell_size:
    name: "Cell Capacity"
    id: cell_capacity
  parallel_count:
    name: "Parallel Cell Count"
    id: parallel_cells
    
  # Individual cell voltages (configure as needed)
  cell_voltage:
    cell_1:
      name: "Cell 1 Voltage"
      id: cell_1_voltage
    cell_2:
      name: "Cell 2 Voltage"
      id: cell_2_voltage
    # ... add more cells as needed up to cell_10
```

### Display Integration Example:

The component supports real-time display updates using LVGL. Here's an example configuration for a 128x128 display:

```yaml
# SPI Configuration for display
spi:
  clk_pin: GPIO40
  mosi_pin: GPIO38

# Display configuration
display:
  - platform: ili9xxx
    id: disp
    dimensions:
      height: 128
      width: 128
    model: st7789v
    data_rate: 80MHz
    cs_pin: GPIO35
    dc_pin: GPIO36
    reset_pin: GPIO34
    invert_colors: true
    update_interval: never
    auto_clear_enabled: false

# LVGL interface with real-time battery monitoring
lvgl:
    color_depth: 16
    bg_color: 0
    style_definitions:
      - id: font_style
        text_font: MONTSERRAT_20
        align: center
        text_color: 0xFFFFFF
        bg_opa: TRANSP
    pages:
      - id: main_page
        widgets:
          - obj:
              widgets:
                - label:
                    id: label_battery_voltage
                    text: "-.-V"
                - label:
                    id: label_battery_soc  
                    text: "---%"
                # ... additional display elements

# Update display when sensor values change
xgt_battery:
  battery_voltage:
    on_value:
      then:
        - lvgl.label.update:
            id: label_battery_voltage
            text: !lambda 'return str_sprintf("%.1fV", x);'
```

## Protocol Details

The component implements the proprietary Makita XGT communication protocol:
- Single-wire serial communication with command echo
- MSB-first bit ordering with bit reversal required
- CRC validation for data integrity
- State machine for non-blocking operation
- Support for up to 10 individual cell voltage readings

## Troubleshooting

### Common Issues:

1. **"UART parity mismatch" warnings**:
   - Ensure `parity: EVEN` is set in your UART configuration
   - Verify you're using the 8E1 format (8 data bits, even parity, 1 stop bit)

2. **No response from battery**:
   - Check that both TX and RX pins have `inverted: true`
   - Verify GPIO pin connections
   - Ensure battery is awake and communicating
   - Check if battery is properly seated and powered

3. **CRC errors**:
   - Verify UART signal inversion settings
   - Check for electromagnetic interference
   - Ensure stable power supply
   - Verify proper grounding

4. **Display not updating**:
   - Check LVGL label IDs match sensor on_value configurations
   - Verify SPI connections for display
   - Ensure display initialization is successful

### Debug Logging:

Set logging level to `DEBUG` to see detailed communication:

```yaml
logger:
  level: DEBUG
  baud_rate: 0  # Disable UART logging to prevent conflict
```

### Performance Optimization:

- **Update Interval**: Default is 5 seconds. Increase for battery conservation
- **Sensor Selection**: Only configure sensors you need to reduce processing overhead
- **Display Updates**: Use conditional updates to minimize LVGL overhead

## Advanced Configuration

### ESP32-S3 Specific Settings:

```yaml
esp32:
  board: lolin_s3_mini
  variant: esp32s3
  flash_size: 4MB
  framework:
    type: esp-idf

psram:  # Enable for display applications
```

### Hardware Button Integration:

```yaml
binary_sensor:
  - platform: gpio
    name: "Button - Left"
    pin:
      number: GPIO0
      inverted: true
      mode:
        input: true
        pullup: true
```

## Credits

This component was made possible thanks to the pioneering work of the following projects and contributors:

- **Protocol Decoding**: [Malvineous/makita-xgt-serial](https://github.com/Malvineous/makita-xgt-serial) - Comprehensive reverse engineering of the Makita XGT serial communication protocol, including electrical signals, data protocol, and checksum algorithms.

- **Hardware Implementation**: [twaymouth/XGT-Tester](https://github.com/twaymouth/XGT-Tester/tree/main) - Basic battery tester implementation for Makita XGT batteries, providing practical examples of hardware interfacing and ESP32 implementation.

Special thanks to:
- **Malvineous** for reverse engineering the UART electrical signals and data protocol
- **ESkri** for reverse engineering the checksum algorithm
- **twaymouth** for the ESP32 hardware implementation and circuit design
- **gcds** for locating relevant patent documentation

Without their foundational work, this ESPHome component would not have been possible.

## License

This component is provided as-is for educational and personal use. 