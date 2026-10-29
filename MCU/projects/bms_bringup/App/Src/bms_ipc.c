#include "bms_ipc.h"
#include "bms_config.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "stm32f1xx.h"
#include <stddef.h>

static QueueHandle_t bms_command_queue = NULL;
static QueueHandle_t bms_acquisition_queue = NULL;
static QueueHandle_t bms_telemetry_queue = NULL;

static volatile uint8_t bms_stop_requested = 0u;
static volatile uint32_t bms_command_drop_count = 0u;
static volatile uint8_t bms_sim_sampling_enabled = 1u;
static volatile uint32_t bms_sample_test_delay_ms = 0u;
static volatile uint32_t bms_comm_test_delay_ms = 0u;

uint8_t Bms_IpcInit(void)
{
    /* 普通命令排队；采集和报告各保留最新一份，读者Peek后数据仍留在邮箱。 */
    bms_command_queue = xQueueCreate(8u, sizeof(BmsCommand));
    bms_telemetry_queue = xQueueCreate(1u, sizeof(BmsTelemetry));
    bms_acquisition_queue = xQueueCreate(1u, sizeof(BmsAcquisition));
    return (bms_command_queue != NULL)
        && (bms_telemetry_queue != NULL)
        && (bms_acquisition_queue != NULL);
}

uint8_t Bms_IpcPublishAcquisition(const BmsAcquisition *acquisition)
{
    return (acquisition != NULL) && (bms_acquisition_queue != NULL)
        && (xQueueOverwrite(bms_acquisition_queue, acquisition) == pdPASS);
}

uint8_t Bms_IpcPeekAcquisition(BmsAcquisition *acquisition)
{
    return (acquisition != NULL) && (bms_acquisition_queue != NULL)
        && (xQueuePeek(bms_acquisition_queue, acquisition, 0u) == pdPASS);
}

uint8_t Bms_IpcPublishReport(const BmsTelemetry *report)
{
    return (report != NULL) && (bms_telemetry_queue != NULL)
        && (xQueueOverwrite(bms_telemetry_queue, report) == pdPASS);
}

uint8_t Bms_IpcWaitReport(BmsTelemetry *report)
{
    return (report != NULL) && (bms_telemetry_queue != NULL)
        && (xQueuePeek(bms_telemetry_queue, report, portMAX_DELAY) == pdPASS);
}

uint8_t Bms_IpcPeekReport(BmsTelemetry *report)
{
    return (report != NULL) && (bms_telemetry_queue != NULL)
        && (xQueuePeek(bms_telemetry_queue, report, 0u) == pdPASS);
}

static void Bms_RecordCommandDrop(void)
{
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    bms_command_drop_count++;
    bms_stop_requested = 1u;
    __set_PRIMASK(saved_primask);
}

void Bms_IpcPostCommand(BmsCommand command)
{
    BaseType_t result;
    BaseType_t task_woken = pdFALSE;
    uint32_t in_isr = __get_IPSR();

    if (command == BMS_CMD_NONE)
    {
        return;
    }
    if (command == BMS_CMD_STOP)
    {
        /* STOP不排在START后面，即使普通命令队列已满也能留下停止请求。 */
        bms_stop_requested = 1u;
        return;
    }
    if (bms_command_queue == NULL)
    {
        Bms_RecordCommandDrop();
        return;
    }

    if (in_isr != 0u)
    {
        result = xQueueSendFromISR(
            bms_command_queue, &command, &task_woken);
    }
    else
    {
        result = xQueueSend(bms_command_queue, &command, 0u);
    }
    if (result != pdPASS)
    {
        Bms_RecordCommandDrop();
    }
    if (in_isr != 0u)
    {
        portYIELD_FROM_ISR(task_woken);
    }
}

BmsCommand Bms_IpcTakeCommand(void)
{
    BmsCommand command = BMS_CMD_NONE;
    uint32_t saved_primask;

    if (bms_command_queue != NULL)
    {
        (void)xQueueReceive(bms_command_queue, &command, 0u);
    }
    /* 临界区只保护取走STOP标志，队列操作放在临界区外。 */
    saved_primask = __get_PRIMASK();
    __disable_irq();
    if (bms_stop_requested != 0u)
    {
        bms_stop_requested = 0u;
        command = BMS_CMD_STOP;
    }
    __set_PRIMASK(saved_primask);

    if ((command == BMS_CMD_STOP) && (bms_command_queue != NULL))
    {
        /* 丢弃停止之前积压的START，避免下一轮意外重新启动。 */
        (void)xQueueReset(bms_command_queue);
    }
    return command;
}

uint8_t Bms_IpcSamplingEnabled(void)
{
    return bms_sim_sampling_enabled;
}

void Bms_IpcSetSamplingEnabled(uint8_t enabled)
{
    bms_sim_sampling_enabled = enabled == 1u ? 1u : 0u;
}

uint32_t Bms_IpcSampleDelayMs(void)
{
    return bms_sample_test_delay_ms;
}

uint32_t Bms_IpcCommDelayMs(void)
{
    return bms_comm_test_delay_ms;
}

void Bms_IpcToggleSampleDelay(void)
{
    bms_sample_test_delay_ms =
        bms_sample_test_delay_ms == 0u ? BMS_TEST_DELAY_MS : 0u;
}

void Bms_IpcToggleCommDelay(void)
{
    bms_comm_test_delay_ms =
        bms_comm_test_delay_ms == 0u ? BMS_TEST_DELAY_MS : 0u;
}
