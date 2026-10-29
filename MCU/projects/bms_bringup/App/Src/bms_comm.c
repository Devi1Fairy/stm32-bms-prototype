#include "bms_comm.h"
#include "bms_config.h"
#include "bms_ipc.h"
#include "bms_protocol.h"
#include "bsp_transport.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint32_t uart_last_ms = 0u;
static uint32_t can_status_last_ms = 0u;
static char uart_tx_buffer[384];
static uint8_t can_status_tx_data[8] = {0};
static uint8_t can_status_rx_data[8] = {0};
static BspCanFrame can_status_rx_frame;
static volatile uint32_t can_status_tx_count = 0u;
static volatile uint32_t can_status_rx_count = 0u;
static volatile uint32_t can_status_error_count = 0u;
static volatile uint32_t can_command_valid_count = 0u;
static volatile uint32_t can_command_invalid_count = 0u;
static volatile uint32_t can_measurement_tx_count = 0u;
static volatile uint32_t bms_uart_tx_error_count = 0u;
static uint8_t can_status_next_sequence = 0u;
static uint8_t can_report_requested = 0u;

/* 字节回调运行在UART中断，只解析短命令并投递，不执行控制或打印。 */
static void Bms_CommOnUartByte(uint8_t byte)
{
    switch (Bms_ProtocolDecodeUart(byte))
    {
        case BMS_ACTION_START: Bms_IpcPostCommand(BMS_CMD_START); break;
        case BMS_ACTION_STOP: Bms_IpcPostCommand(BMS_CMD_STOP); break;
        case BMS_ACTION_CLEAR: Bms_IpcPostCommand(BMS_CMD_CLEAR); break;
        case BMS_ACTION_PAUSE: Bms_IpcSetSamplingEnabled(0u); break;
        case BMS_ACTION_RESUME: Bms_IpcSetSamplingEnabled(1u); break;
        case BMS_ACTION_DELAY_SAMPLE: Bms_IpcToggleSampleDelay(); break;
        case BMS_ACTION_DELAY_COMM: Bms_IpcToggleCommDelay(); break;
        default: break;
    }
}

static void Bms_HandleCanCommand(const BspCanFrame *frame)
{
    BmsProtocolAction action = Bms_ProtocolDecodeCan(
        frame->id, frame->extended, frame->remote, frame->length, frame->data);

    if (action == BMS_ACTION_IGNORED)
    {
        return;
    }
    if (action == BMS_ACTION_INVALID)
    {
        can_command_invalid_count++;
        return;
    }
    switch (action)
    {
        case BMS_ACTION_START: Bms_IpcPostCommand(BMS_CMD_START); break;
        case BMS_ACTION_STOP: Bms_IpcPostCommand(BMS_CMD_STOP); break;
        case BMS_ACTION_CLEAR: Bms_IpcPostCommand(BMS_CMD_CLEAR); break;
        case BMS_ACTION_QUERY: can_report_requested = 1u; break;
        default: return;
    }
    /* 格式有效不代表执行获准，状态机仍作最终决定。 */
    can_command_valid_count++;
}

static uint8_t Bms_CanSendMeasurements(
    const BmsTelemetry *report, uint32_t now_ms)
{
    BspCanFrame frame = {0};
    frame.id = BMS_CAN_MEASUREMENT_ID;
    frame.length = 8u;
    if ((Bms_ProtocolEncodeMeasurements(
            &report->sample, &bms_protocol_limits, now_ms, frame.data) == 0u)
        || (Bsp_CanSend(&frame) == 0u))
    {
        return 0u;
    }
    can_measurement_tx_count++;
    return 1u;
}

static void Bms_CanStatusPoll(const BmsTelemetry *report, uint32_t now_ms)
{
    BspCanFrame frame = {0};
    uint8_t periodic_due;
    int32_t received = Bsp_CanReceive(&can_status_rx_frame);

    if (received < 0)
    {
        can_status_error_count++;
        return;
    }
    if (received > 0)
    {
        can_status_rx_count++;
        memcpy(can_status_rx_data, can_status_rx_frame.data, sizeof(can_status_rx_data));
        Bms_HandleCanCommand(&can_status_rx_frame);
    }

    periodic_due = (uint8_t)(
        (uint32_t)(now_ms - can_status_last_ms) >= BMS_CAN_PERIOD_MS);
    if ((periodic_due == 0u) && (can_report_requested == 0u))
    {
        return;
    }
    if (periodic_due != 0u)
    {
        can_status_last_ms = now_ms;
    }
    if (Bsp_CanFreeMailboxes() < 2u)
    {
        can_status_error_count++;
        return;
    }

    if (Bms_ProtocolEncodeStatus(
            &report->status, can_status_next_sequence, can_status_tx_data) == 0u)
    {
        can_status_error_count++;
        return;
    }
    frame.id = BMS_CAN_STATUS_ID;
    frame.length = 8u;
    memcpy(frame.data, can_status_tx_data, sizeof(frame.data));
    if (Bsp_CanSend(&frame) == 0u)
    {
        can_status_error_count++;
        return;
    }
    can_status_tx_count++;
    can_status_next_sequence = (uint8_t)(can_status_next_sequence + 1u);

    if (Bms_CanSendMeasurements(report, now_ms) == 0u)
    {
        can_status_error_count++;
        return;
    }
    can_report_requested = 0u;
}

uint8_t Bms_CommInit(void)
{
    uint32_t now_ms;
    if ((Bsp_CanInit() == 0u)
        || (Bsp_UartRxInit(Bms_CommOnUartByte) == 0u))
    {
        return 0u;
    }
    now_ms = HAL_GetTick();
    uart_last_ms = now_ms;
    can_status_last_ms = now_ms;
    return 1u;
}

void Bms_CommRecoverRx(void)
{
    Bsp_UartRxPoll();
}

uint8_t Bms_CommPoll(const BmsTelemetry *report, uint32_t now_ms)
{
    int length;

    if (report == NULL)
    {
        return 0u;
    }
    Bms_CanStatusPoll(report, now_ms);
    if ((uint32_t)(now_ms - uart_last_ms) < BMS_UART_PERIOD_MS)
    {
        return 1u;
    }

    uart_last_ms = now_ms;
    length = snprintf(
        uart_tx_buffer, sizeof(uart_tx_buffer),
        "BMS state=%u, faults=0x%08lX, latched=0x%08lX, "
        "load=%u, clear=%u, cell0_mv=%ld, temp_est_dC=%ld, "
        "i_ma=%ld, iv=%u, i_src=%u, i_age=%lu, "
        "ina=%ld, bus_mv=%lu, sample=%lu, ctrl=%lu, gap_max=%lu\r\n",
        (unsigned int)report->status.state,
        (unsigned long)report->status.active_faults,
        (unsigned long)report->status.latched_faults,
        (unsigned int)report->status.load_enabled,
        (unsigned int)report->status.clear_allowed,
        (long)report->sample.cell_mv[0].value,
        (long)report->sample.temperature_dC.value,
        (long)report->sample.current_ma.value,
        (unsigned int)report->sample.current_ma.valid,
        (unsigned int)report->sample.current_ma.source,
        (unsigned long)(uint32_t)(now_ms - report->sample.current_ma.timestamp_ms),
        (long)report->ina_result,
        (unsigned long)report->bus_mv,
        (unsigned long)report->sample_count,
        (unsigned long)report->control_count,
        (unsigned long)report->control_max_interval_ms);

    if ((length <= 0) || ((size_t)length >= sizeof(uart_tx_buffer)))
    {
        return 0u;
    }
    if (Bsp_UartWrite((const uint8_t *)uart_tx_buffer, (uint16_t)length) == 0u)
    {
        bms_uart_tx_error_count++;
    }
    return 1u;
}
