#include "bms_config.h"

/* 台架演示参数；目前固定配置，通信任务不能直接修改。 */
const BmsControllerConfig bms_config = {
    .uv_threshold_mv = 3000,
    .ov_threshold_mv = 4200,
    .over_temp_dC = 600,
    .discharge_limit_ma = 80,
    .charge_limit_ma = 80,
    .voltage_max_age_ms = BMS_VOLTAGE_MAX_AGE_MS,
    .temperature_max_age_ms = BMS_TEMPERATURE_MAX_AGE_MS,
    .current_max_age_ms = BMS_CURRENT_MAX_AGE_MS,
    .recovery = {
        .cell_min_mv = 3200,
        .cell_max_mv = 4100,
        .max_temperature_dC = 550,
        .discharge_limit_ma = 60,
        .charge_limit_ma = 60,
        .voltage_max_age_ms = BMS_VOLTAGE_MAX_AGE_MS,
        .temperature_max_age_ms = BMS_TEMPERATURE_MAX_AGE_MS,
        .current_max_age_ms = BMS_CURRENT_MAX_AGE_MS,
        .stable_time_ms = 2000u
    }
};

/* 厂家参数仍未确认，以下模型只用于未标定温度估算。 */
const NtcModel bms_ntc_model = {
    .pullup_ohm = 10000.0f,
    .r25_ohm = 10000.0f,
    .beta_k = 3950.0f,
    .min_temperature_c = -20.0f,
    .max_temperature_c = 100.0f,
    .rail_margin = 10u
};

/* 上报与保护使用相同的时效门限，避免一边判超时另一边仍报有效。 */
const BmsProtocolLimits bms_protocol_limits = {
    BMS_VOLTAGE_MAX_AGE_MS,
    BMS_TEMPERATURE_MAX_AGE_MS,
    BMS_CURRENT_MAX_AGE_MS
};
