#include <assert.h>
#include <stdio.h>
#include "bms_sampling.h"
#include "bsp_analog.h"
#include "ina226.h"
#include "i2c.h"

I2C_HandleTypeDef hi2c1;
static uint32_t tick = 100u;
static uint32_t pot = 1023u, ntc = 2020u;
static uint8_t adc_ok = 1u, pot_ok = 1u, ntc_ok = 1u;
static uint16_t config, ready = 8u, shunt = 1920u, bus = 4232u;
static int fail_reg = -1;
uint32_t HAL_GetTick(void) { return tick; }
uint8_t Bsp_AnalogInit(void) { return adc_ok; }
uint8_t Bsp_AnalogRead(BspAnalogInput input, uint32_t *raw)
{
    if (input == BSP_ANALOG_POT && pot_ok) { *raw = pot; return 1u; }
    if (input == BSP_ANALOG_NTC && ntc_ok) { *raw = ntc; return 1u; }
    return 0u;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(
    I2C_HandleTypeDef *i2c, uint16_t address, uint32_t trials, uint32_t timeout)
{
    assert(i2c == &hi2c1 && trials == 1u && timeout == 5u);
    return address == (0x44u << 1) ? HAL_OK : HAL_ERROR;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(
    I2C_HandleTypeDef *i2c, uint16_t address, uint16_t reg,
    uint16_t reg_size, uint8_t *data, uint16_t length, uint32_t timeout)
{
    assert(i2c == &hi2c1 && address == (0x44u << 1));
    assert(reg == 0u && reg_size == I2C_MEMADD_SIZE_8BIT && length == 2u && timeout == 5u);
    config = (uint16_t)((uint16_t)data[0] << 8 | data[1]);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(
    I2C_HandleTypeDef *i2c, uint16_t address, uint16_t reg,
    uint16_t reg_size, uint8_t *data, uint16_t length, uint32_t timeout)
{
    uint16_t value;
    assert(i2c == &hi2c1 && address == (0x44u << 1));
    assert(reg_size == I2C_MEMADD_SIZE_8BIT && length == 2u && timeout == 5u);
    if ((int)reg == fail_reg) return HAL_ERROR;
    switch (reg)
    {
        case 0xFEu: value = 0x5449u; break;
        case 0xFFu: value = 0x2260u; break;
        case 0x00u: value = config; break;
        case 0x06u: value = ready; break;
        case 0x01u: value = shunt; break;
        case 0x02u: value = bus; break;
        default: return HAL_ERROR;
    }
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
    return HAL_OK;
}
int main(void)
{
    BmsAcquisition frame = {0};
    Ina226Diagnostics diag;
    adc_ok = 0u;
    assert(!Bms_SamplingInit());
    adc_ok = 1u;
    assert(Bms_SamplingInit());
    Ina226_GetDiagnostics(&diag);
    assert(diag.found && diag.configured && diag.address7 == 0x44u && config == 0x4527u);
    Bms_SamplingRead(&frame, 90u);
    assert(frame.sample.cell_mv[0].value == 3200);
    assert(frame.sample.cell_mv[0].timestamp_ms == 90u);
    assert(frame.sample.cell_mv[1].value == 3800 && frame.sample.cell_mv[1].source == BMS_SOURCE_SIMULATED);
    assert(frame.sample.temperature_dC.valid && frame.sample.temperature_dC.value == 256);
    assert(frame.sample.current_ma.valid && frame.sample.current_ma.value == 48);
    assert(frame.sample.current_ma.timestamp_ms == 100u);
    assert(frame.ina_result == INA226_NEW_DATA && frame.bus_mv == 5290u && frame.sample_count == 1u);
    /* 无新转换：电流值和时间均保留，不能让旧数据一直看起来很新。 */
    ready = 0u; tick = 200u;
    Bms_SamplingRead(&frame, 190u);
    assert(frame.sample.current_ma.value == 48 && frame.sample.current_ma.timestamp_ms == 100u);
    assert(frame.sample.current_ma.valid && frame.ina_result == INA226_NO_DATA);
    /* 部分读取失败：不能只更新总线电压或电流中的一半。 */
    ready = 8u; fail_reg = 2; pot_ok = 0u; ntc_ok = 0u; tick = 300u;
    Bms_SamplingRead(&frame, 290u);
    assert(!frame.sample.current_ma.valid && frame.sample.current_ma.timestamp_ms == 100u);
    assert(frame.sample.current_ma.value == 48 && frame.bus_mv == 5290u);
    assert(!frame.sample.cell_mv[0].valid && frame.sample.cell_mv[0].timestamp_ms == 190u);
    assert(!frame.sample.temperature_dC.valid && frame.sample.temperature_dC.timestamp_ms == 190u);
    assert(frame.ina_result == INA226_ERROR);
    Ina226_GetDiagnostics(&diag);
    assert(diag.read_count == 1u && diag.error_count == 1u);
    fail_reg = -1; pot_ok = ntc_ok = 1u; shunt = (uint16_t)(65536u - 1920u); tick = 400u;
    pot = 4095u; ntc = 0u;
    Bms_SamplingRead(&frame, 390u);
    assert(frame.sample.cell_mv[0].value == 4400 && frame.sample.cell_mv[0].valid);
    assert(!frame.sample.temperature_dC.valid);
    assert(frame.sample.current_ma.value == -48 && frame.sample.current_ma.valid);
    assert(frame.sample.current_ma.timestamp_ms == 400u);
    ready = 0u; tick = 500u;
    Bms_SamplingRead(&frame, 490u);
    assert(frame.sample.current_ma.timestamp_ms == 400u && frame.sample.current_ma.valid);
    ready = 8u; shunt = 0x7FFFu;
    Bms_SamplingRead(&frame, 590u);
    assert(!frame.sample.current_ma.valid && frame.ina_result == INA226_ERROR);
    Bms_SamplingRead(NULL, 690u);
    assert(frame.sample_count == 6u);
    puts("ADC mapping, INA226 byte order/sign, NEW/NO_DATA/ERROR and timestamps passed.");
    return 0;
}
