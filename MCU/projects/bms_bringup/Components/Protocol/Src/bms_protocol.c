#include "bms_protocol.h"
#include <stddef.h>
#include <string.h>

BmsProtocolAction Bms_ProtocolDecodeCan(
    uint32_t id, uint8_t extended, uint8_t remote,
    uint8_t length, const uint8_t *data)
{
    if ((extended != 0u) || (id != BMS_CAN_COMMAND_ID))
    {
        return BMS_ACTION_IGNORED;
    }
    if ((remote != 0u) || (length != 1u) || (data == NULL))
    {
        return BMS_ACTION_INVALID;
    }

    switch (data[0])
    {
        case 0x01u: return BMS_ACTION_START;
        case 0x02u: return BMS_ACTION_STOP;
        case 0x03u: return BMS_ACTION_CLEAR;
        case 0x04u: return BMS_ACTION_QUERY;
        default: return BMS_ACTION_INVALID;
    }
}

BmsProtocolAction Bms_ProtocolDecodeUart(uint8_t byte)
{
    switch (byte)
    {
        case 'S': return BMS_ACTION_START;
        case 'T': return BMS_ACTION_STOP;
        case 'C': return BMS_ACTION_CLEAR;
        case 'P': return BMS_ACTION_PAUSE;
        case 'R': return BMS_ACTION_RESUME;
        case 'A': return BMS_ACTION_DELAY_SAMPLE;
        case 'D': return BMS_ACTION_DELAY_COMM;
        default: return BMS_ACTION_IGNORED;
    }
}

uint8_t Bms_ProtocolEncodeStatus(
    const BmsStatus *status, uint8_t sequence, uint8_t data[8])
{
    if ((status == NULL) || (data == NULL))
    {
        return 0u;
    }
    data[0] = (uint8_t)status->state;
    data[1] = status->load_enabled;
    data[2] = status->clear_allowed;
    data[3] = sequence;
    data[4] = (uint8_t)status->active_faults;
    data[5] = (uint8_t)(status->active_faults >> 8);
    data[6] = (uint8_t)(status->active_faults >> 16);
    data[7] = (uint8_t)(status->active_faults >> 24);
    return 1u;
}

static uint8_t Bms_ProtocolFresh(
    const BmsMeasurement *measurement, uint32_t now_ms, uint32_t max_age_ms)
{
    return (measurement->valid == 1u)
        && (measurement->source >= BMS_SOURCE_SIMULATED)
        && (measurement->source <= BMS_SOURCE_EXTERNAL)
        && ((uint32_t)(now_ms - measurement->timestamp_ms) < max_age_ms);
}

uint8_t Bms_ProtocolEncodeMeasurements(
    const BmsSample *sample, const BmsProtocolLimits *limits,
    uint32_t now_ms, uint8_t data[8])
{
    uint16_t voltage = 0u;
    uint16_t temperature = 0u;
    uint16_t current = 0u;
    uint8_t flags = 0u;
    uint8_t sources = 0u;

    if ((sample == NULL) || (limits == NULL) || (data == NULL))
    {
        return 0u;
    }

    if (Bms_ProtocolFresh(
            &sample->cell_mv[0], now_ms, limits->voltage_max_age_ms)
        && (sample->cell_mv[0].value >= 0)
        && (sample->cell_mv[0].value <= 65535))
    {
        voltage = (uint16_t)sample->cell_mv[0].value;
        flags |= 0x01u;
        sources |= (uint8_t)sample->cell_mv[0].source;
    }
    if (Bms_ProtocolFresh(
            &sample->temperature_dC, now_ms, limits->temperature_max_age_ms)
        && (sample->temperature_dC.value >= -32768)
        && (sample->temperature_dC.value <= 32767))
    {
        temperature = (uint16_t)sample->temperature_dC.value;
        flags |= 0x02u;
        sources |= (uint8_t)((uint8_t)sample->temperature_dC.source << 2);
    }
    if (Bms_ProtocolFresh(
            &sample->current_ma, now_ms, limits->current_max_age_ms)
        && (sample->current_ma.value >= -32768)
        && (sample->current_ma.value <= 32767))
    {
        current = (uint16_t)sample->current_ma.value;
        flags |= 0x04u;
        sources |= (uint8_t)((uint8_t)sample->current_ma.source << 4);
    }

    data[0] = (uint8_t)voltage;
    data[1] = (uint8_t)(voltage >> 8);
    data[2] = (uint8_t)temperature;
    data[3] = (uint8_t)(temperature >> 8);
    data[4] = (uint8_t)current;
    data[5] = (uint8_t)(current >> 8);
    data[6] = flags;
    data[7] = sources;
    return 1u;
}
