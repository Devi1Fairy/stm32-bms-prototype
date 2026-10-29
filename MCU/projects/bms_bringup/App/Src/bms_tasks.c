#include "bms_tasks.h"
#include "bms_config.h"
#include "bms_ipc.h"
#include "bms_sampling.h"
#include "bms_comm.h"
#include "bsp_output.h"
#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

/* 控制器只由保护任务修改，其他任务通过报告副本读取结果。 */
static BmsSample bms_sample = {0};
static BmsController bms_controller;
static int32_t bms_control_ina_result = -1;
static uint32_t bms_control_bus_mv = 0u;
static volatile uint32_t bms_control_sample_count = 0u;
static uint32_t bms_control_last_ms = 0u;

static volatile uint32_t bms_rtos_loop_count = 0u;
static volatile uint32_t bms_rtos_led_count = 0u;
static volatile uint32_t bms_rtos_sample_loop_count = 0u;
static volatile uint32_t bms_rtos_comm_loop_count = 0u;
static volatile uint32_t bms_rtos_app_stack_free_words = 0u;
static volatile uint32_t bms_rtos_led_stack_free_words = 0u;
static volatile uint32_t bms_rtos_sample_stack_free_words = 0u;
static volatile uint32_t bms_rtos_comm_stack_free_words = 0u;
static volatile uint32_t bms_rtos_heap_free_bytes = 0u;
static volatile uint32_t bms_control_cycle_count = 0u;
static volatile uint32_t bms_control_max_interval_ms = 0u;

static void Bms_UpdateIndicators(void)
{
    Bsp_OutputApply(
        bms_controller.machine.load_enabled,
        bms_controller.machine.state == BMS_STATE_FAULT_LOCK ? 1u : 0u);
}

static void Bms_PublishTelemetry(void)
{
    /* 只交出需要显示的结果，不把控制器的内部恢复计时器传给通信层。 */
    BmsTelemetry report = {0};
    report.sample = bms_sample;
    report.status.state = bms_controller.machine.state;
    report.status.active_faults = bms_controller.active_faults;
    report.status.latched_faults = bms_controller.machine.latched_faults;
    report.status.load_enabled = bms_controller.machine.load_enabled;
    report.status.clear_allowed = bms_controller.clear_allowed;
    report.ina_result = bms_control_ina_result;
    report.bus_mv = bms_control_bus_mv;
    report.sample_count = bms_control_sample_count;
    report.control_count = bms_control_cycle_count;
    report.control_max_interval_ms = bms_control_max_interval_ms;
    (void)Bms_IpcPublishReport(&report);
}

static uint8_t Bms_TakeLatestAcquisition(void)
{
    BmsAcquisition acquisition;
    if (Bms_IpcPeekAcquisition(&acquisition) == 0u)
    {
        return 0u;
    }
    bms_sample = acquisition.sample;
    bms_control_ina_result = acquisition.ina_result;
    bms_control_bus_mv = acquisition.bus_mv;
    bms_control_sample_count = acquisition.sample_count;
    return 1u;
}

static void Bms_RunControlCycle(uint32_t startup_ms, uint8_t *sample_seen)
{
    uint32_t now_ms;
    uint32_t interval_ms;
    BmsCommand command;

    bms_rtos_loop_count++;
    if (Bms_TakeLatestAcquisition() != 0u)
    {
        *sample_seen = 1u;
    }
    /* 先复制采样，再取当前时间，避免新采样的时间戳比本轮now_ms还晚。 */
    now_ms = HAL_GetTick();
    /* 等待首份采样期间保持关断；超过等待上限后，由保护逻辑检查缺失数据。 */
    if ((*sample_seen != 0u)
        || ((uint32_t)(now_ms - startup_ms) >= BMS_STARTUP_WAIT_MS))
    {
        command = Bms_IpcTakeCommand();
        Bms_ControllerStep(
            &bms_controller, &bms_sample, &bms_config,
            command, BMS_DEMO_SELF_TEST_PASSED, now_ms);
    }
    Bms_UpdateIndicators();

    now_ms = HAL_GetTick();
    if (bms_control_cycle_count != 0u)
    {
        interval_ms = (uint32_t)(now_ms - bms_control_last_ms);
        if (interval_ms > bms_control_max_interval_ms)
        {
            bms_control_max_interval_ms = interval_ms;
        }
    }
    bms_control_last_ms = now_ms;
    bms_control_cycle_count++;
    Bms_PublishTelemetry();
}

void Bms_RunTask(void)
{
    TickType_t last_wake;
    uint32_t startup_ms;
    uint8_t sample_seen = 0u;

    Bms_ControllerInit(&bms_controller);
    Bms_UpdateIndicators();
    startup_ms = HAL_GetTick();
    last_wake = xTaskGetTickCount();
    /* 20ms检查一次采样时效、命令和状态机，不等待I2C或串口发送。 */
    for (;;)
    {
        Bms_RunControlCycle(startup_ms, &sample_seen);
        bms_rtos_app_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        bms_rtos_heap_free_bytes = (uint32_t)xPortGetFreeHeapSize();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_CONTROL_PERIOD_MS));
    }
}

void Bms_RunSampleTask(void)
{
    BmsAcquisition acquisition = {0};
    TickType_t last_wake;
    uint32_t delay_ms;

    /* ADC与INA226只有本任务访问，读取成功后按值发布一份采集快照。 */
    if (Bms_SamplingInit() == 0u)
    {
        Error_Handler();
    }
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_SAMPLE_PERIOD_MS));
        bms_rtos_sample_loop_count++;
        delay_ms = Bms_IpcSampleDelayMs();
        if (delay_ms != 0u)
        {
            osDelay(delay_ms);
            last_wake = xTaskGetTickCount();
        }
        if (Bms_IpcSamplingEnabled() == 1u)
        {
            Bms_SamplingRead(&acquisition, HAL_GetTick());
            (void)Bms_IpcPublishAcquisition(&acquisition);
        }
        bms_rtos_sample_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    }
}

void Bms_RunCommTask(void)
{
    BmsTelemetry report;
    uint32_t now_ms;
    uint32_t delay_ms;
    uint32_t stack_last_ms;

    /* 首份报告产生前先等待，不能发送未初始化的状态。 */
    if ((Bms_IpcWaitReport(&report) == 0u) || (Bms_CommInit() == 0u))
    {
        Error_Handler();
    }
    stack_last_ms = HAL_GetTick();
    for (;;)
    {
        bms_rtos_comm_loop_count++;
        Bms_CommRecoverRx();
        (void)Bms_IpcPeekReport(&report);
        now_ms = HAL_GetTick();
        if (Bms_CommPoll(&report, now_ms) == 0u)
        {
            Error_Handler();
        }
        /* 栈水位需要扫描栈空间，按日志周期统计，避免每1ms反复扫描。 */
        if ((uint32_t)(now_ms - stack_last_ms) >= BMS_UART_PERIOD_MS)
        {
            stack_last_ms = now_ms;
            bms_rtos_comm_stack_free_words =
                (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        }
        delay_ms = Bms_IpcCommDelayMs();
        osDelay(delay_ms == 0u ? 1u : delay_ms);
    }
}

void Bms_RunHeartbeatTask(void)
{
    /* 心跳只表示本任务仍被调度，不能代替整个系统的健康检查。 */
    TickType_t last_wake = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_LED_PERIOD_MS));
        Bsp_OutputToggleHeartbeat();
        bms_rtos_led_count++;
        bms_rtos_led_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    }
}
