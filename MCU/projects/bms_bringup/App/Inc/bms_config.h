#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

#include "bms_controller.h"
#include "ntc.h"
#include "bms_protocol.h"

#define BMS_SAMPLE_PERIOD_MS       100u
#define BMS_CONTROL_PERIOD_MS       20u
#define BMS_STARTUP_WAIT_MS        1000u
#define BMS_UART_PERIOD_MS         200u
#define BMS_CAN_PERIOD_MS         1000u
#define BMS_LED_PERIOD_MS          500u
#define BMS_TEST_DELAY_MS         1000u
#define BMS_INA226_SHUNT_MOHM       100u
#define BMS_VOLTAGE_MAX_AGE_MS      500u
#define BMS_TEMPERATURE_MAX_AGE_MS  500u
#define BMS_CURRENT_MAX_AGE_MS      500u

#define BMS_DEMO_CELL0_MIN_MV      2800u
#define BMS_DEMO_CELL0_SPAN_MV     1600u

/* 仍是演示常量，不能把首份数据等待当作完整硬件自检。 */
#define BMS_DEMO_SELF_TEST_PASSED     1u

extern const BmsControllerConfig bms_config;
extern const NtcModel bms_ntc_model;
extern const BmsProtocolLimits bms_protocol_limits;

#endif
