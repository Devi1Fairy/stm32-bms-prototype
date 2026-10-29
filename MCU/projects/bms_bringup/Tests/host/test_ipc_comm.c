#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_ipc.h"
#include "bms_comm.h"
#include "bsp_transport.h"
#include "FreeRTOS.h"
#include "stm32f1xx.h"

static BspUartByteHandler rx_handler;
static BspCanFrame received, sent[16];
static unsigned int pending, sent_count, mailboxes = 3u, fail_send_at;
static uint32_t tick;
static char log_text[384];
static uint8_t uart_ok = 1u;
uint32_t HAL_GetTick(void) { return tick; }
uint8_t Bsp_CanInit(void) { return 1u; }
uint8_t Bsp_UartRxInit(BspUartByteHandler handler) { rx_handler = handler; return 1u; }
void Bsp_UartRxPoll(void) {}
uint32_t Bsp_CanFreeMailboxes(void) { return mailboxes; }
int32_t Bsp_CanReceive(BspCanFrame *frame)
{
    if (!pending) return 0;
    pending = 0u;
    *frame = received;
    return 1;
}
uint8_t Bsp_CanSend(const BspCanFrame *frame)
{
    if (fail_send_at && sent_count + 1u == fail_send_at) return 0u;
    assert(sent_count < 16u);
    sent[sent_count++] = *frame;
    return 1u;
}
uint8_t Bsp_UartWrite(const uint8_t *text, uint16_t length)
{
    assert(length < sizeof(log_text));
    memcpy(log_text, text, length);
    log_text[length] = '\0';
    return uart_ok;
}
static void CanCommand(uint8_t command, BmsTelemetry *report, uint32_t now)
{
    received = (BspCanFrame){0x201u, 0u, 0u, 1u, {command}};
    pending = 1u;
    assert(Bms_CommPoll(report, now));
}
int main(void)
{
    unsigned int i;
    BmsAcquisition acquisition = {0}, acquired;
    BmsTelemetry report = {0}, copied;
    /* 队列未创建、普通队列满，均能留下独立STOP请求。 */
    Bms_IpcPostCommand(BMS_CMD_START);
    assert(Bms_IpcTakeCommand() == BMS_CMD_STOP);
    assert(Bms_IpcInit());
    assert(!Bms_IpcPeekAcquisition(&acquired));
    Bms_IpcPostCommand(BMS_CMD_START);
    Bms_IpcPostCommand(BMS_CMD_CLEAR);
    assert(Bms_IpcTakeCommand() == BMS_CMD_START);
    assert(Bms_IpcTakeCommand() == BMS_CMD_CLEAR);
    for (i = 0u; i < 9u; ++i) Bms_IpcPostCommand(BMS_CMD_START);
    mock_primask = 1u;
    assert(Bms_IpcTakeCommand() == BMS_CMD_STOP);
    assert(mock_primask == 1u);
    mock_primask = 0u;
    assert(Bms_IpcTakeCommand() == BMS_CMD_NONE);
    mock_ipsr = 16u;
    Bms_IpcPostCommand(BMS_CMD_START);
    assert(mock_isr_sends == 1u && mock_yields == 1u);
    Bms_IpcPostCommand(BMS_CMD_STOP);
    mock_ipsr = 0u;
    assert(Bms_IpcTakeCommand() == BMS_CMD_STOP);
    assert(Bms_IpcTakeCommand() == BMS_CMD_NONE);
    acquisition.sample_count = 3u;
    assert(Bms_IpcPublishAcquisition(&acquisition));
    acquisition.sample_count = 4u;
    assert(Bms_IpcPeekAcquisition(&acquired) && acquired.sample_count == 3u);
    assert(Bms_IpcPublishAcquisition(&acquisition));
    assert(Bms_IpcPeekAcquisition(&acquired) && acquired.sample_count == 4u);
    assert(!Bms_IpcPublishAcquisition(NULL));
    report.control_count = 12u;
    assert(Bms_IpcPublishReport(&report));
    report.control_count = 13u;
    assert(Bms_IpcWaitReport(&copied) && copied.control_count == 12u);
    assert(Bms_IpcPeekReport(&copied) && copied.control_count == 12u);
    assert(!Bms_IpcPeekReport(NULL));
    assert(Bms_CommInit());
    mock_ipsr = 16u;
    rx_handler('S');
    rx_handler('T');
    rx_handler('P'); assert(!Bms_IpcSamplingEnabled());
    rx_handler('R'); assert(Bms_IpcSamplingEnabled());
    rx_handler('A'); assert(Bms_IpcSampleDelayMs() == 1000u);
    rx_handler('A'); assert(Bms_IpcSampleDelayMs() == 0u);
    rx_handler('D'); assert(Bms_IpcCommDelayMs() == 1000u);
    rx_handler('D'); assert(Bms_IpcCommDelayMs() == 0u);
    mock_ipsr = 0u;
    assert(Bms_IpcTakeCommand() == BMS_CMD_STOP);
    assert(Bms_IpcTakeCommand() == BMS_CMD_NONE);
    CanCommand(1u, &report, 10u);
    CanCommand(3u, &report, 11u);
    assert(Bms_IpcTakeCommand() == BMS_CMD_START);
    assert(Bms_IpcTakeCommand() == BMS_CMD_CLEAR);
    CanCommand(0xFFu, &report, 12u);
    assert(Bms_IpcTakeCommand() == BMS_CMD_NONE);
    mailboxes = 1u;
    CanCommand(4u, &report, 13u);
    assert(sent_count == 0u);
    mailboxes = 3u;
    fail_send_at = 2u;
    assert(Bms_CommPoll(&report, 14u));
    assert(sent_count == 1u && sent[0].id == 0x301u && sent[0].data[3] == 0u);
    fail_send_at = 0u;
    assert(Bms_CommPoll(&report, 15u));
    assert(sent_count == 3u && sent[1].data[3] == 1u && sent[2].id == 0x302u);
    assert(Bms_CommPoll(&report, 16u) && sent_count == 3u);
    report.status.state = BMS_STATE_STANDBY;
    report.sample.cell_mv[0] = (BmsMeasurement){3449, 100u, 1u, BMS_SOURCE_ADC};
    report.sample.temperature_dC = (BmsMeasurement){268, 100u, 1u, BMS_SOURCE_ADC};
    report.sample.current_ma = (BmsMeasurement){0, 99u, 1u, BMS_SOURCE_EXTERNAL};
    report.ina_result = 1;
    report.bus_mv = 5290u;
    report.sample_count = 831u;
    report.control_count = 4611u;
    report.control_max_interval_ms = 20u;
    assert(Bms_CommPoll(&report, 200u));
    assert(strcmp(log_text,
        "BMS state=1, faults=0x00000000, latched=0x00000000, load=0, clear=0, "
        "cell0_mv=3449, temp_est_dC=268, i_ma=0, iv=1, i_src=3, i_age=101, "
        "ina=1, bus_mv=5290, sample=831, ctrl=4611, gap_max=20\r\n") == 0);
    uart_ok = 0u;
    assert(Bms_CommPoll(&report, 400u));
    assert(Bms_CommPoll(&report, 1000u) && sent_count == 5u);
    assert(!Bms_CommPoll(NULL, 1000u));
    puts("STOP/FIFO/ISR routing, snapshot copies, query retry and UART fixture passed.");
    return 0;
}
