#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bms_protocol.h"

int main(void)
{
    BmsSample sample = {0};
    BmsStatus status = {BMS_STATE_FAULT_LOCK, 0x12345678u, 0xFFFFFFFFu, 0u, 1u};
    BmsProtocolLimits limits = {500u, 500u, 500u};
    uint8_t bytes[8];
    uint8_t command;
    const uint8_t expected_status[8] = {3u, 0u, 1u, 0xFFu, 0x78u, 0x56u, 0x34u, 0x12u};
    const uint8_t expected_measurement[8] = {0xD8u, 0x0Eu, 0xCEu, 0xFFu, 0xD0u, 0xFFu, 7u, 0x3Au};
    assert(Bms_ProtocolEncodeStatus(&status, 255u, bytes));
    assert(memcmp(bytes, expected_status, 8u) == 0);
    assert(!Bms_ProtocolEncodeStatus(NULL, 0u, bytes));
    assert(!Bms_ProtocolEncodeStatus(&status, 0u, NULL));
    sample.cell_mv[0] = (BmsMeasurement){3800, 100u, 1u, BMS_SOURCE_ADC};
    sample.temperature_dC = (BmsMeasurement){-50, 100u, 1u, BMS_SOURCE_ADC};
    sample.current_ma = (BmsMeasurement){-48, 100u, 1u, BMS_SOURCE_EXTERNAL};
    assert(Bms_ProtocolEncodeMeasurements(&sample, &limits, 599u, bytes));
    assert(memcmp(bytes, expected_measurement, 8u) == 0);
    assert(Bms_ProtocolEncodeMeasurements(&sample, &limits, 600u, bytes));
    assert(memcmp(bytes, (uint8_t[8]){0}, 8u) == 0);
    sample.cell_mv[0].timestamp_ms = UINT32_MAX - 49u;
    assert(Bms_ProtocolEncodeMeasurements(&sample, &limits, 50u, bytes));
    assert(bytes[6] == 1u && bytes[7] == 2u);
    sample.cell_mv[0].value = 65536;
    sample.temperature_dC = (BmsMeasurement){32768, 50u, 1u, BMS_SOURCE_ADC};
    sample.current_ma = (BmsMeasurement){-32769, 50u, 1u, BMS_SOURCE_EXTERNAL};
    assert(Bms_ProtocolEncodeMeasurements(&sample, &limits, 50u, bytes));
    assert(memcmp(bytes, (uint8_t[8]){0}, 8u) == 0);
    sample.current_ma = (BmsMeasurement){0, 50u, 2u, BMS_SOURCE_EXTERNAL};
    assert(Bms_ProtocolEncodeMeasurements(&sample, &limits, 50u, bytes) && bytes[6] == 0u);
    sample.current_ma.valid = 1u;
    sample.current_ma.source = BMS_SOURCE_NONE;
    assert(Bms_ProtocolEncodeMeasurements(&sample, &limits, 50u, bytes) && bytes[6] == 0u);
    assert(!Bms_ProtocolEncodeMeasurements(NULL, &limits, 0u, bytes));
    assert(!Bms_ProtocolEncodeMeasurements(&sample, NULL, 0u, bytes));
    assert(!Bms_ProtocolEncodeMeasurements(&sample, &limits, 0u, NULL));
    for (command = 1u; command <= 4u; ++command)
    {
        assert(Bms_ProtocolDecodeCan(0x201u, 0u, 0u, 1u, &command)
            == (BmsProtocolAction)(BMS_ACTION_START + command - 1u));
    }
    command = 0u;
    assert(Bms_ProtocolDecodeCan(0x201u, 0u, 0u, 1u, &command) == BMS_ACTION_INVALID);
    assert(Bms_ProtocolDecodeCan(0x201u, 0u, 1u, 1u, &command) == BMS_ACTION_INVALID);
    assert(Bms_ProtocolDecodeCan(0x201u, 0u, 0u, 8u, &command) == BMS_ACTION_INVALID);
    assert(Bms_ProtocolDecodeCan(0x201u, 0u, 0u, 1u, NULL) == BMS_ACTION_INVALID);
    assert(Bms_ProtocolDecodeCan(0x201u, 1u, 0u, 1u, &command) == BMS_ACTION_IGNORED);
    assert(Bms_ProtocolDecodeCan(0x301u, 0u, 0u, 1u, &command) == BMS_ACTION_IGNORED);
    assert(Bms_ProtocolDecodeUart('S') == BMS_ACTION_START);
    assert(Bms_ProtocolDecodeUart('T') == BMS_ACTION_STOP);
    assert(Bms_ProtocolDecodeUart('C') == BMS_ACTION_CLEAR);
    assert(Bms_ProtocolDecodeUart('P') == BMS_ACTION_PAUSE);
    assert(Bms_ProtocolDecodeUart('R') == BMS_ACTION_RESUME);
    assert(Bms_ProtocolDecodeUart('A') == BMS_ACTION_DELAY_SAMPLE);
    assert(Bms_ProtocolDecodeUart('D') == BMS_ACTION_DELAY_COMM);
    assert(Bms_ProtocolDecodeUart('s') == BMS_ACTION_IGNORED);
    assert(Bms_ProtocolDecodeUart('\r') == BMS_ACTION_IGNORED);
    puts("CAN byte layout, freshness, signed values and command checks passed.");
    return 0;
}
