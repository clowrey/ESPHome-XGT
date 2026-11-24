import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_TEMPERATURE, 
    DEVICE_CLASS_BATTERY,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    ICON_BATTERY,
    ICON_THERMOMETER,
    ICON_FLASH,
    ICON_CURRENT_AC,
    ENTITY_CATEGORY_DIAGNOSTIC,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_ENERGY,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

CODEOWNERS = ["@your-username"]
DEPENDENCIES = ["uart", "sensor", "text_sensor"]

CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_TEMPERATURE = "battery_temperature"
CONF_BATTERY_CHARGE = "battery_charge"
CONF_BATTERY_HEALTH = "battery_health"
CONF_NUM_CHARGES = "num_charges"
CONF_CELL_SIZE = "cell_size"
CONF_PARALLEL_COUNT = "parallel_count"
CONF_CELL_VOLTAGE = "cell_voltage"
CONF_MIN_CELL_VOLTAGE = "min_cell_voltage"
CONF_MAX_CELL_VOLTAGE = "max_cell_voltage"
CONF_CELL_DIVERGENCE = "cell_divergence"
CONF_REGISTERS = "registers"
CONF_ADDRESS = "address"
CONF_SCAN = "scan"
CONF_SWEEP_PER_CYCLE = "sweep_per_cycle"
CONF_COUNTER_CHARGE = "counter_charge"
CONF_COUNTER_CAPACITY = "counter_capacity"
CONF_COUNTER_CURRENT = "counter_current"
CONF_COUNTER_POWER = "counter_power"
CONF_COUNTER_ENERGY = "counter_energy"
CONF_PACK_SOC = "pack_soc"

REGISTER_SENSOR_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend({cv.Required(CONF_ADDRESS): cv.hex_uint16_t})

xgt_battery_ns = cg.esphome_ns.namespace("xgt_battery")
XGTBattery = xgt_battery_ns.class_("XGTBattery", cg.Component, uart.UARTDevice)

# Define cell voltage schema for up to 10 cells
CELL_VOLTAGE_SCHEMA = cv.Schema({
    cv.Optional(f"cell_{i+1}"): sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=3,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        icon=ICON_FLASH,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ) for i in range(10)
})

def validate_uart_settings(config):
    """Validate that UART is configured correctly for XGT battery communication
    
    XGT Battery UART Requirements:
    - Baud Rate: 9600
    - Parity: EVEN (8E1 format) - CRITICAL
    - Half Duplex: true (recommended for simplified wiring)
    - TX Pin: inverted: true (required)
    
    Recommended half duplex configuration:
    uart:
      - id: xgt_uart
        baud_rate: 9600
        half_duplex: true
        parity: EVEN
        tx_pin: 
          number: GPIO14
          inverted: true
    """
    # Note: Actual UART validation happens during the UART component validation
    # This function serves as documentation of requirements and best practices
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(XGTBattery),
        cv.Optional(CONF_UPDATE_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
        
        # Main battery sensors
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_BATTERY_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_THERMOMETER,
        ),
        cv.Optional(CONF_BATTERY_CHARGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_BATTERY,
        ),
        cv.Optional(CONF_BATTERY_HEALTH): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_BATTERY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_MIN_CELL_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=3,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_MAX_CELL_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=3,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_CELL_DIVERGENCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=3,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_FLASH,
        ),

        
        # Diagnostic sensors
        cv.Optional(CONF_NUM_CHARGES): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_CURRENT_AC,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_CELL_SIZE): sensor.sensor_schema(
            unit_of_measurement="mAh",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PARALLEL_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        
        # Cell voltages
        cv.Optional(CONF_CELL_VOLTAGE): CELL_VOLTAGE_SCHEMA,

        # Register discovery: raw signed 16-bit values of unmapped registers
        cv.Optional(CONF_REGISTERS): cv.ensure_list(REGISTER_SENSOR_SCHEMA),
        cv.Optional(CONF_SCAN, default=True): cv.boolean,
        cv.Optional(CONF_SWEEP_PER_CYCLE, default=24): cv.int_range(min=0, max=1000),

        # Estimates from long-frame parameter 0x120A (charge counter in mA*s, unverified)
        cv.Optional(CONF_COUNTER_CHARGE): sensor.sensor_schema(
            unit_of_measurement="mAh",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_BATTERY,
        ),
        cv.Optional(CONF_COUNTER_CAPACITY): sensor.sensor_schema(
            unit_of_measurement="mAh",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_BATTERY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_COUNTER_CURRENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_COUNTER_POWER): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_COUNTER_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT_HOURS,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_PACK_SOC): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    validate_uart_settings,
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(var.set_scan_enabled(config[CONF_SCAN]))
    cg.add(var.set_sweep_per_cycle(config[CONF_SWEEP_PER_CYCLE]))

    for key, setter in (
        (CONF_COUNTER_CHARGE, var.set_counter_charge_sensor),
        (CONF_COUNTER_CAPACITY, var.set_counter_capacity_sensor),
        (CONF_COUNTER_CURRENT, var.set_counter_current_sensor),
        (CONF_COUNTER_POWER, var.set_counter_power_sensor),
        (CONF_COUNTER_ENERGY, var.set_counter_energy_sensor),
        (CONF_PACK_SOC, var.set_pack_soc_sensor),
    ):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))

    for reg in config.get(CONF_REGISTERS, []):
        sens = await sensor.new_sensor(reg)
        cg.add(var.add_register_sensor(reg[CONF_ADDRESS], sens))
    
    # Configure main battery sensors
    if CONF_BATTERY_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage_sensor(sens))
        
    if CONF_BATTERY_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_TEMPERATURE])
        cg.add(var.set_battery_temperature_sensor(sens))
        
    if CONF_BATTERY_CHARGE in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_CHARGE])
        cg.add(var.set_battery_charge_sensor(sens))
        
    if CONF_BATTERY_HEALTH in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_HEALTH])
        cg.add(var.set_battery_health_sensor(sens))
        
    if CONF_MIN_CELL_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_MIN_CELL_VOLTAGE])
        cg.add(var.set_min_cell_voltage_sensor(sens))
        
    if CONF_MAX_CELL_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_MAX_CELL_VOLTAGE])
        cg.add(var.set_max_cell_voltage_sensor(sens))
        
    if CONF_CELL_DIVERGENCE in config:
        sens = await sensor.new_sensor(config[CONF_CELL_DIVERGENCE])
        cg.add(var.set_cell_divergence_sensor(sens))

        
    # Configure diagnostic sensors
    if CONF_NUM_CHARGES in config:
        sens = await sensor.new_sensor(config[CONF_NUM_CHARGES])
        cg.add(var.set_num_charges_sensor(sens))
        
    if CONF_CELL_SIZE in config:
        sens = await sensor.new_sensor(config[CONF_CELL_SIZE])
        cg.add(var.set_cell_size_sensor(sens))
        
    if CONF_PARALLEL_COUNT in config:
        sens = await sensor.new_sensor(config[CONF_PARALLEL_COUNT])
        cg.add(var.set_parallel_count_sensor(sens))
        
    # Configure cell voltage sensors
    if CONF_CELL_VOLTAGE in config:
        cell_config = config[CONF_CELL_VOLTAGE]
        for i in range(10):
            cell_key = f"cell_{i+1}"
            if cell_key in cell_config:
                sens = await sensor.new_sensor(cell_config[cell_key])
                cg.add(var.set_cell_voltage_sensor(i, sens)) 