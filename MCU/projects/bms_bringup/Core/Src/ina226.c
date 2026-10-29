#include "ina226.h"
#include <stddef.h>
#include <string.h>

/* 16次平均，分流/总线各1.1ms，连续转换。 */
#define INA226_CONFIG_VALUE  0x4527u
#define INA226_TIMEOUT_MS    5u

/* 设备状态留在驱动内部，不暴露为可任意写入的全局变量。 */
static I2C_HandleTypeDef *ina226_i2c = NULL;
static uint16_t ina226_shunt_mohm = 0u;
static Ina226Diagnostics ina226_diag = {0};

/* I2C按高字节在前读寄存器，应用层不需要处理字节序。 */
static uint8_t Ina226_ReadReg(
    uint8_t address7, uint8_t reg, uint16_t *value)
{
    uint8_t data[2];

    if ((ina226_i2c == NULL) || (value == NULL))
    {
        return 0u;
    }

    if (HAL_I2C_Mem_Read(
            ina226_i2c, (uint16_t)(address7 << 1),
            reg, I2C_MEMADD_SIZE_8BIT,
            data, 2u, INA226_TIMEOUT_MS) != HAL_OK)
    {
        return 0u;
    }

    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return 1u;
}

/* 写寄存器同样先发高字节，地址只在HAL边界左移。 */
static uint8_t Ina226_WriteReg(uint8_t reg, uint16_t value)
{
    uint8_t data[2];

    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;

    return HAL_I2C_Mem_Write(
        ina226_i2c, (uint16_t)(ina226_diag.address7 << 1),
        reg, I2C_MEMADD_SIZE_8BIT,
        data, 2u, INA226_TIMEOUT_MS) == HAL_OK;
}

static void Ina226_Probe(void)
{
    uint8_t address7;
    uint16_t manufacturer;
    uint16_t die_id;

    for (address7 = 0x40u; address7 <= 0x4Fu; ++address7)
    {
        if (HAL_I2C_IsDeviceReady(
                ina226_i2c, (uint16_t)(address7 << 1),
                1u, INA226_TIMEOUT_MS) != HAL_OK)
        {
            continue;
        }

        ina226_diag.ack_count++;

        if ((Ina226_ReadReg(address7, 0xFEu, &manufacturer) == 0u)
            || (Ina226_ReadReg(address7, 0xFFu, &die_id) == 0u))
        {
            continue;
        }

        ina226_diag.manufacturer = manufacturer;
        ina226_diag.die_id = die_id;

        if ((manufacturer == 0x5449u)
            && ((die_id & 0xFFF0u) == 0x2260u))
        {
            ina226_diag.address7 = address7;
            ina226_diag.found = 1u;
            return;
        }
    }
}

uint8_t Ina226_Init(I2C_HandleTypeDef *i2c, uint16_t shunt_mohm)
{
    uint16_t config;

    memset(&ina226_diag, 0, sizeof(ina226_diag));
    ina226_i2c = NULL;
    ina226_shunt_mohm = 0u;

    if ((i2c == NULL) || (shunt_mohm == 0u))
    {
        return 0u;
    }

    ina226_i2c = i2c;
    ina226_shunt_mohm = shunt_mohm;
    Ina226_Probe();

    if (ina226_diag.found == 0u)
    {
        return 0u;
    }

    if ((Ina226_WriteReg(0x00u, INA226_CONFIG_VALUE) == 0u)
        || (Ina226_ReadReg(
                ina226_diag.address7, 0x00u, &config) == 0u))
    {
        return 0u;
    }

    ina226_diag.configured = (uint8_t)(config == INA226_CONFIG_VALUE);
    return ina226_diag.configured;
}

/* 失败只增加计数，不把测量值改成0或刷新旧时间。 */
static Ina226Result Ina226_ReadError(void)
{
    ina226_diag.error_count++;
    return INA226_ERROR;
}

Ina226Result Ina226_Read(Ina226Sample *sample)
{
    uint16_t status;
    uint16_t shunt;
    uint16_t bus;
    int32_t raw;
    int32_t numerator;
    int32_t denominator;
    Ina226Sample next;

    if ((sample == NULL) || (ina226_i2c == NULL)
        || (ina226_diag.configured == 0u) || (ina226_shunt_mohm == 0u))
    {
        return Ina226_ReadError();
    }

    /* CVRF在0x06的bit3，读取该寄存器会清除这个标志。 */
    if (Ina226_ReadReg(ina226_diag.address7, 0x06u, &status) == 0u)
    {
        return Ina226_ReadError();
    }

    if ((status & 0x0008u) == 0u)
    {
        return INA226_NO_DATA;
    }

    if ((Ina226_ReadReg(ina226_diag.address7, 0x01u, &shunt) == 0u)
        || (Ina226_ReadReg(ina226_diag.address7, 0x02u, &bus) == 0u))
    {
        return Ina226_ReadError();
    }

    /* 保留原型现有的保守量程检查。 */
    if ((shunt == 0x7FFFu) || (shunt == 0x8000u) || (bus > 28800u))
    {
        return Ina226_ReadError();
    }

    raw = (shunt & 0x8000u) != 0u
        ? (int32_t)shunt - 65536 : (int32_t)shunt;

    /* 每码2.5微伏：mA = raw × 5 / (2 × 分流电阻毫欧数)。 */
    numerator = raw * 5;
    denominator = (int32_t)ina226_shunt_mohm * 2;
    next.current_ma = numerator >= 0
        ? (numerator + denominator / 2) / denominator
        : -((-numerator + denominator / 2) / denominator);

    /* 总线每码1.25mV，四舍五入保存为mV。 */
    next.bus_mv = ((uint32_t)bus * 5u + 2u) / 4u;
    next.timestamp_ms = HAL_GetTick();

    /* 所有步骤成功后，再一次性交出新结果。 */
    *sample = next;
    ina226_diag.shunt_raw = raw;
    ina226_diag.read_count++;
    return INA226_NEW_DATA;
}

void Ina226_GetDiagnostics(Ina226Diagnostics *diagnostics)
{
    if (diagnostics != NULL)
    {
        *diagnostics = ina226_diag;
    }
}
