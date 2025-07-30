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
    """Validate that UART is configured correctly for XGT battery communication"""
    # Note: UART validation happens during the UART component validation
    # This function serves as documentation of requirements
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