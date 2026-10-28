/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "adc.h"
#include "can.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bms_types.h"
#include "bms_controller.h"
#include <stdio.h>
#include <math.h>
#include "queue.h"
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* 一份完整报告由业务任务生成，通信任务只使用自己的副本。 */
typedef struct
{
    BmsSample sample;
    BmsController controller;
    int32_t ina_result;
    uint32_t bus_mv;
    uint32_t control_count;
    uint32_t control_max_interval_ms;
} BmsTelemetry;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LED_PERIOD_MS       500u
#define UART_PERIOD_MS      200u
#define BMS_PERIOD_MS       100u

#define CAN_STATUS_PERIOD_MS  1000u
#define CAN_STATUS_STD_ID     0x301u
#define CAN_COMMAND_STD_ID  0x201u
#define CAN_COMMAND_START   0x01u
#define CAN_COMMAND_STOP    0x02u
#define CAN_COMMAND_CLEAR       0x03u
#define CAN_COMMAND_QUERY       0x04u
#define CAN_MEASUREMENT_STD_ID  0x302u

#define ADC_MAX_CODE          4095u
#define ADC_REFERENCE_MV      3300u
#define DEMO_CELL0_MIN_MV     2800u
#define DEMO_CELL0_SPAN_MV    1600u

/* 本步用这个模拟结果通过自检，不代表已经实现硬件自检。 */
#define DEMO_SELF_TEST_PASSED  1u
/* 平均值最多使用最近4次成功采样。 */
#define ADC_FILTER_WINDOW     4u

/* 以下是演示用暂定模型，NTC参数未经厂家确认或标定。 */
#define NTC_PULLUP_OHM         10000.0f
#define NTC_R25_OHM            10000.0f
#define NTC_BETA_K              3950.0f

/* 单位0.1℃；手温测试时临时改为280/260，测试后恢复。 */
#define DEMO_OVER_TEMP_DC       600
#define DEMO_RECOVER_TEMP_DC    550

/* 实物R100对应100毫欧；不要写成100欧。 */
#define INA226_SHUNT_MOHM  100u

/* 16次平均，分流/总线各1.1ms，连续转换。 */
#define INA226_CONFIG_VALUE  0x4527u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* 只用于调试观察，不参与保护判断。 */
static volatile uint32_t bms_rtos_loop_count = 0u;
static volatile uint32_t bms_rtos_led_count = 0u;

static volatile uint32_t bms_rtos_app_stack_free_words = 0u;
static volatile uint32_t bms_rtos_led_stack_free_words = 0u;
static volatile uint32_t bms_rtos_heap_free_bytes = 0u;

/* 三个动作分别记录上次执行时间。 */
static uint32_t uart_last_ms = 0u;
static uint32_t bms_last_ms = 0u;

/* 采样快照和控制器都需要在多次循环之间保留。 */
static BmsSample bms_sample = {0};
static BmsController bms_controller;
static char uart_tx_buffer[384];

/* 队列保存命令值，最多暂存8条普通命令。 */
static QueueHandle_t bms_command_queue = NULL;

/* STOP单独保留，不依赖普通队列是否还有空位。 */
static volatile uint8_t bms_stop_requested = 0u;

/* 普通命令未能入队的次数，用于观察拥塞。 */
static volatile uint32_t bms_command_drop_count = 0u;
/* 1：生成新模拟采样；0：保留旧快照，用来验证采样超时。 */
static volatile uint8_t bms_sim_sampling_enabled = 1u;

/* HAL把收到的一个字节写到这里；接收结束后由回调读取。 */
static uint8_t uart_rx_byte = 0u;
/* 中断只提出重启接收的请求，具体恢复放在主循环完成。 */
static volatile uint8_t uart_rx_restart_required = 0u;
/* ADC原始读数，以及按3.3V参考估算的实际输入毫伏值。 */

static volatile uint32_t bms_adc_raw = 0u;
static volatile uint32_t bms_adc_input_mv = 0u;
/* 电位器位置映射成的模拟单体电压，不是PC0引脚电压。 */
static volatile int32_t bms_adc_cell0_mv = 0;
/* 上一次实际读取是否成功；暂停采样时保留上次结果。 */
static volatile uint8_t bms_adc_read_ok = 0u;
static volatile uint32_t bms_adc_error_count = 0u;
/* 保存最近几次原始码，以及计算平均值需要的记录。 */
static uint32_t bms_adc_filter_window[ADC_FILTER_WINDOW] = {0};
static uint32_t bms_adc_filter_sum = 0u;
static uint32_t bms_adc_filter_index = 0u;
static uint32_t bms_adc_filter_last_ms = 0u;
static volatile uint32_t bms_adc_filter_count = 0u;

/* 滤波结果用于显示；是否有新有效数据仍看read_ok和时间戳。 */
static volatile uint32_t bms_adc_filtered_raw = 0u;
static volatile uint32_t bms_adc_filtered_input_mv = 0u;

/* 未标定估算温度，单位0.1℃；采样失败时保留旧值。 */
static volatile int32_t bms_ntc_temp_est_dC = 0;

/* 仅用于软件演示，不能直接作为真实电池的保护参数。 */
static const BmsControllerConfig bms_config = {
    .uv_threshold_mv = 3000,
    .ov_threshold_mv = 4200,
    .over_temp_dC = DEMO_OVER_TEMP_DC,          /* 60.0℃ */
    .discharge_limit_ma = 80,
    .charge_limit_ma = 80,
    .voltage_max_age_ms = 500u,
    .temperature_max_age_ms = 500u,
    .current_max_age_ms = 500u,
    .recovery = {
        .cell_min_mv = 3200,
        .cell_max_mv = 4100,
        .max_temperature_dC = DEMO_RECOVER_TEMP_DC,
        .discharge_limit_ma = 60,
        .charge_limit_ma = 60,
        .voltage_max_age_ms = 500u,
        .temperature_max_age_ms = 500u,
        .current_max_age_ms = 500u,
        .stable_time_ms = 2000u
    }
};

/* 上次安排发送状态帧的时间，单位ms。 */
static uint32_t can_status_last_ms = 0u;
/* 最近一次发送、接收的8字节数据，便于Watch查看。 */
static uint8_t can_status_tx_data[8] = {0};
static uint8_t can_status_rx_data[8] = {0};
static CAN_RxHeaderTypeDef can_status_rx_header = {0};
/* tx记录提交成功次数；rx记录从FIFO取出帧的次数。 */
static volatile uint32_t can_status_tx_count = 0u;
static volatile uint32_t can_status_rx_count = 0u;
/* 记录软件检查遇到异常的次数，不是CAN硬件错误计数器。 */
static volatile uint32_t can_status_error_count = 0u;
/* 下一帧要使用的序号，超过255后从0重新开始。 */
static uint8_t can_status_next_sequence = 0u;
/* 有效/无效命令格式计数；有效不代表状态机同意执行。 */
static volatile uint32_t can_command_valid_count = 0u;
static volatile uint32_t can_command_invalid_count = 0u;
/* 查询到来时置1，两帧报告提交成功后清零。 */
static uint8_t can_report_requested = 0u;
static volatile uint32_t can_measurement_tx_count = 0u;

/* NTC模块AO的采样结果；这里暂不换算成摄氏温度。 */
static volatile uint32_t bms_ntc_raw = 0u;
static volatile uint32_t bms_ntc_input_mv = 0u;
static volatile uint8_t bms_ntc_read_ok = 0u;
static volatile uint32_t bms_ntc_error_count = 0u;

/* INA226身份检查结果，供Keil Watch观察。 */
static volatile uint8_t bms_ina226_found = 0u;
static volatile uint8_t bms_ina226_address7 = 0u;
static volatile uint8_t bms_ina226_ack_count = 0u;
static volatile uint16_t bms_ina226_manufacturer = 0u;
static volatile uint16_t bms_ina226_die_id = 0u;

/* INA226真实测量的调试值，本步暂不替换BMS里的模拟电流。 */
static volatile uint8_t bms_ina226_configured = 0u;
static volatile int32_t bms_ina226_result = -1;
static volatile int32_t bms_ina226_shunt_raw = 0;
static volatile int32_t bms_ina226_current_ma = 0;
static volatile uint32_t bms_ina226_bus_mv = 0u;
static volatile uint32_t bms_ina226_last_ms = 0u;
static volatile uint32_t bms_ina226_read_count = 0u;
static volatile uint32_t bms_ina226_error_count = 0u;

/* 最新报告只留一份；普通命令仍使用原来的8项队列。 */
static QueueHandle_t bms_telemetry_queue = NULL;

static volatile uint32_t bms_rtos_comm_loop_count = 0u;
static volatile uint32_t bms_rtos_comm_stack_free_words = 0u;
static volatile uint32_t bms_uart_tx_error_count = 0u;

/* 记录控制检查完成的次数与相邻完成时刻的最大间隔。 */
static volatile uint32_t bms_control_cycle_count = 0u;
static uint32_t bms_control_last_ms = 0u;
static volatile uint32_t bms_control_max_interval_ms = 0u;

/* 仅用于实验：UART发送D，在0和1000ms通信延迟之间切换。 */
static volatile uint32_t bms_comm_test_delay_ms = 0u;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* 记录入队失败，并请求停止；这里只短暂保护几个内存操作。 */
static void Bms_RecordCommandDrop(void)
{
    uint32_t saved_primask = __get_PRIMASK();

    __disable_irq();
    bms_command_drop_count++;
    bms_stop_requested = 1u;
    __set_PRIMASK(saved_primask);
}

/* 每个控制周期最多取一条普通命令，STOP优先。 */
static BmsCommand Bms_TakePendingCommand(void)
{
    BmsCommand command = BMS_CMD_NONE;
    uint32_t saved_primask;

    /* 等待时间为0，队列为空时立即继续检查保护。 */
    (void)xQueueReceive(bms_command_queue, &command, 0u);

    saved_primask = __get_PRIMASK();
    __disable_irq();

    if (bms_stop_requested != 0u)
    {
        bms_stop_requested = 0u;
        command = BMS_CMD_STOP;
    }

    __set_PRIMASK(saved_primask);

    if (command == BMS_CMD_STOP)
    {
        /* 丢弃积压命令，避免停止后又执行旧的START。 */
        (void)xQueueReset(bms_command_queue);
    }

    return command;
}
/* CAN从任务调用，串口从中断调用，分别使用对应的队列接口。 */
static void Bms_PostCommand(BmsCommand command)
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
        /* 队列满时立即返回，并通过STOP请求停止。 */
        Bms_RecordCommandDrop();
    }

    if (in_isr != 0u)
    {
        /* 需要时，在退出中断后安排一次任务切换。 */
        portYIELD_FROM_ISR(task_woken);
    }
}

/* 只解析本项目的启动/停止命令，不直接改变状态或控制GPIO。 */
static void Bms_HandleCanCommand(
    const CAN_RxHeaderTypeDef *header,
    const uint8_t *data)
{
    BmsCommand command;

    /* 其他ID和扩展帧不属于本步协议，直接忽略。 */
    if ((header->IDE != CAN_ID_STD)
        || (header->StdId != CAN_COMMAND_STD_ID))
    {
        return;
    }

    /* 命令必须是数据帧，而且恰好携带一个字节。 */
    if ((header->RTR != CAN_RTR_DATA) || (header->DLC != 1u))
    {
        can_command_invalid_count++;
        return;
    }

    switch (data[0])
    {
        case CAN_COMMAND_START:
            command = BMS_CMD_START;
            break;

        case CAN_COMMAND_STOP:
            command = BMS_CMD_STOP;
            break;

        case CAN_COMMAND_CLEAR:
            command = BMS_CMD_CLEAR;
            break;

        case CAN_COMMAND_QUERY:
            /* 查询不覆盖正在等待处理的启动、停止或清除请求。 */
            can_report_requested = 1u;
            can_command_valid_count++;
            return;

        default:
            can_command_invalid_count++;
            return;
    }

    Bms_PostCommand(command);

    /* 格式有效不代表允许启动，最终由状态机判断。 */
    can_command_valid_count++;
}

/* 按控制器最终结果更新负载和指示灯。 */
static void Bms_UpdateIndicators(const BmsController *controller)
{
    /* 负载默认关闭，板载LED默认熄灭。 */
    GPIO_PinState load_level = GPIO_PIN_RESET;
    GPIO_PinState run_level = GPIO_PIN_SET;
    GPIO_PinState fault_level = GPIO_PIN_SET;

    if (controller->machine.load_enabled == 1u)
    {
        load_level = GPIO_PIN_SET;
        run_level = GPIO_PIN_RESET;
    }

    if (controller->machine.state == BMS_STATE_FAULT_LOCK)
    {
        fault_level = GPIO_PIN_RESET;
    }

    /* 先执行负载控制，再更新两颗状态灯。 */
    HAL_GPIO_WritePin(
        LOAD_EN_GPIO_Port, LOAD_EN_Pin, load_level);

    HAL_GPIO_WritePin(
        LED_RUN_GPIO_Port, LED_RUN_Pin, run_level);

    HAL_GPIO_WritePin(
        LED_FAULT_GPIO_Port, LED_FAULT_Pin, fault_level);
}

/* 只在本次ADC读取成功后调用，返回最近最多4次读数的平均值。 */
static uint32_t Bms_FilterAdcRaw(uint32_t raw, uint32_t now_ms)
{
    /* 首次采样、读取失败后，或间隔已到超时门限时，重新建立窗口。 */
    if ((bms_adc_filter_count == 0u)
        || ((uint32_t)(now_ms - bms_adc_filter_last_ms)
            >= bms_config.voltage_max_age_ms))
    {
        bms_adc_filter_sum = 0u;
        bms_adc_filter_index = 0u;
        bms_adc_filter_count = 0u;
    }

    if (bms_adc_filter_count < ADC_FILTER_WINDOW)
    {
        /* 刚启动时，只平均已经收到的读数，不把空位置当作0。 */
        bms_adc_filter_count++;
    }
    else
    {
        /* 窗口已满：先从总和中减去即将被替换的最旧读数。 */
        bms_adc_filter_sum -=
            bms_adc_filter_window[bms_adc_filter_index];
    }

    bms_adc_filter_window[bms_adc_filter_index] = raw;
    bms_adc_filter_sum += raw;
    bms_adc_filter_index++;
    if (bms_adc_filter_index >= ADC_FILTER_WINDOW)
    {
        bms_adc_filter_index = 0u;
    }
    bms_adc_filter_last_ms = now_ms;

    /* 加半个除数再整除，让平均值四舍五入到整数ADC码。 */
    return (bms_adc_filter_sum + bms_adc_filter_count / 2u)
        / bms_adc_filter_count;
}

/* 两路共用ADC1：每次先选通道，再启动一次转换。 */
static uint8_t Bms_ReadAdcChannel(uint32_t channel, uint32_t *raw_out)
{
    ADC_ChannelConfTypeDef config = {0};
    uint32_t raw;

    if (raw_out == NULL)
    {
        return 0u;
    }

    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
    {
        return 0u;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        (void)HAL_ADC_Stop(&hadc1);
        return 0u;
    }

    /* 等待参数为2ms；失败也尝试停止ADC，不使用无限等待。 */
    if (HAL_ADC_PollForConversion(&hadc1, 2u) != HAL_OK)
    {
        (void)HAL_ADC_Stop(&hadc1);
        return 0u;
    }

    raw = HAL_ADC_GetValue(&hadc1);
    if ((HAL_ADC_Stop(&hadc1) != HAL_OK) || (raw > ADC_MAX_CODE))
    {
        return 0u;
    }

    *raw_out = raw;
    return 1u;
}

/* PC0仍读电位器，保持原来的模拟单体电压映射。 */
static uint8_t Bms_ReadPotentiometer(void)
{
    uint32_t raw;

    if (Bms_ReadAdcChannel(ADC_CHANNEL_10, &raw) == 0u)
    {
        return 0u;
    }

    bms_adc_raw = raw;
    bms_adc_input_mv =
        (raw * ADC_REFERENCE_MV + ADC_MAX_CODE / 2u) / ADC_MAX_CODE;
    bms_adc_cell0_mv = (int32_t)(
        DEMO_CELL0_MIN_MV
        + (raw * DEMO_CELL0_SPAN_MV + ADC_MAX_CODE / 2u) / ADC_MAX_CODE);
    return 1u;
}

/* PC1读取NTC模块AO，只报告原始码和估算毫伏值。 */
static uint8_t Bms_ReadNtcVoltage(void)
{
    uint32_t raw;

    if (Bms_ReadAdcChannel(ADC_CHANNEL_11, &raw) == 0u)
    {
        return 0u;
    }

    bms_ntc_raw = raw;
    bms_ntc_input_mv =
        (raw * ADC_REFERENCE_MV + ADC_MAX_CODE / 2u) / ADC_MAX_CODE;

    /* 电源轨附近暂按输入异常处理；不能据此确诊某一种断线。 */
    if ((raw <= 10u) || (raw >= ADC_MAX_CODE - 10u))
    {
        return 0u;
    }

    return 1u;
}

/* 将AO原始码换算为估算温度；仅适用于暂定的NTC下臂模型。 */
static uint8_t Bms_NtcRawToEstimatedTemperature(
    uint32_t raw, int32_t *temperature_dC)
{
    float resistance_ohm;
    float inverse_kelvin;
    float temperature_c;
    float scaled;

    if (temperature_dC == NULL)
    {
        return 0u;
    }

    /* 两轨附近不换算，避免除零、log(0)和明显异常输入。 */
    if ((raw <= 10u) || (raw >= ADC_MAX_CODE - 10u))
    {
        return 0u;
    }

    resistance_ohm =
        NTC_PULLUP_OHM * (float)raw / (float)(ADC_MAX_CODE - raw);

    /* Beta公式使用开尔文和自然对数；298.15K对应25℃。 */
    inverse_kelvin =
        1.0f / 298.15f + logf(resistance_ohm / NTC_R25_OHM) / NTC_BETA_K;
    if (!(inverse_kelvin > 0.0f))
    {
        return 0u;
    }

    temperature_c = 1.0f / inverse_kelvin - 273.15f;

    /* 软件只接受估算的-20～100℃；这个判断也会拒绝NaN。 */
    if (!(temperature_c >= -20.0f && temperature_c <= 100.0f))
    {
        return 0u;
    }

    scaled = temperature_c * 10.0f;
    /* 正负温度都四舍五入到0.1℃，然后保存为整数。 */
    *temperature_dC =
        (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
    return 1u;
}

/* 第一节电压由电位器模拟，温度由NTC估算，其余电压和电流仍模拟。 */
static void Bms_UpdateDemoSample(BmsSample *sample, uint32_t now_ms)
{
    uint32_t i;
    int32_t temperature_est_dC = 0;
    
    /* 从下标1开始，只生成第2、3节的固定模拟电压。 */
    for (i = 1u; i < BMS_CELL_COUNT; ++i)
    {
        sample->cell_mv[i].value = 3800;
        sample->cell_mv[i].timestamp_ms = now_ms;
        sample->cell_mv[i].valid = 1u;
        sample->cell_mv[i].source = BMS_SOURCE_SIMULATED;
    }

    sample->temperature_dC.source = BMS_SOURCE_ADC;
    bms_ntc_read_ok = Bms_ReadNtcVoltage();
    if ((bms_ntc_read_ok == 1u)
        && (Bms_NtcRawToEstimatedTemperature(
                bms_ntc_raw, &temperature_est_dC) == 1u))
    {
        bms_ntc_temp_est_dC = temperature_est_dC;
        sample->temperature_dC.value = temperature_est_dC;
        sample->temperature_dC.timestamp_ms = now_ms;
        sample->temperature_dC.valid = 1u;
    }
    else
    {
        /* 失败不刷新旧值和时间戳，保护模块会看到valid=0。 */
        bms_ntc_read_ok = 0u;
        sample->temperature_dC.valid = 0u;
        bms_ntc_error_count++;
    }

    sample->cell_mv[0].source = BMS_SOURCE_ADC;
    bms_adc_read_ok = Bms_ReadPotentiometer();

    if (bms_adc_read_ok == 1u)
    {
        bms_adc_filtered_raw = Bms_FilterAdcRaw(bms_adc_raw, now_ms);
        bms_adc_filtered_input_mv =
            (bms_adc_filtered_raw * ADC_REFERENCE_MV + ADC_MAX_CODE / 2u)
            / ADC_MAX_CODE;

        /* 保护仍使用本次原始读数映射的电压，避免显示滤波延后触发。 */
        sample->cell_mv[0].value = bms_adc_cell0_mv;
        sample->cell_mv[0].timestamp_ms = now_ms;
        sample->cell_mv[0].valid = 1u;
    }
    else
    {
        /* 失败时标记无效，保留旧值和旧时间，交给保护逻辑处理。 */
        sample->cell_mv[0].valid = 0u;
        bms_adc_error_count++;

        /* 丢弃旧窗口，下次成功采样时从新读数开始计算。 */
        bms_adc_filter_count = 0u;
    }
}

/* 配置接收过滤器并启动CAN，只在上电初始化时调用一次。 */
static void Bms_CanStart(void)
{
    CAN_FilterTypeDef filter = {0};

    /* 掩码全为0：先允许所有ID进入FIFO0，方便初期调试。 */
    filter.FilterBank = 0u;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;

    if ((HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK)
        || (HAL_CAN_Start(&hcan) != HAL_OK))
    {
        Error_Handler();
    }
}

/* 检查原始采样记录，发送报文时不会刷新它的时间戳。 */
static uint8_t Bms_CanMeasurementFresh(
    const BmsMeasurement *measurement,
    uint32_t now_ms,
    uint32_t max_age_ms)
{
    return (measurement->valid == 1u)
        && (measurement->source >= BMS_SOURCE_SIMULATED)
        && (measurement->source <= BMS_SOURCE_AFE)
        && ((uint32_t)(now_ms - measurement->timestamp_ms) < max_age_ms);
}

/* 上报一节模拟单体电压、温度、电流及数据标志。 */
static uint8_t Bms_CanSendMeasurements(
    const BmsTelemetry *report, uint32_t now_ms)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox = 0u;
    uint8_t data[8] = {0};
    uint16_t voltage = 0u;
    uint16_t temperature = 0u;
    uint16_t current = 0u;
    uint8_t flags = 0u;
    uint8_t sources = 0u;

    /* 超时、无效或超出报文范围时，保持占位0，不设置有效位。 */
    if (Bms_CanMeasurementFresh(
            &report->sample.cell_mv[0], now_ms,
            bms_config.voltage_max_age_ms)
        && (report->sample.cell_mv[0].value >= 0)
        && (report->sample.cell_mv[0].value <= 65535))
    {
        voltage = (uint16_t)report->sample.cell_mv[0].value;
        flags |= 0x01u;
        sources |= (uint8_t)report->sample.cell_mv[0].source;
    }

    if (Bms_CanMeasurementFresh(
            &report->sample.temperature_dC, now_ms,
            bms_config.temperature_max_age_ms)
        && (report->sample.temperature_dC.value >= -32768)
        && (report->sample.temperature_dC.value <= 32767))
    {
        /* 负数转换为16位无符号数后，按补码字节发送。 */
        temperature = (uint16_t)report->sample.temperature_dC.value;
        flags |= 0x02u;
        sources |= (uint8_t)(
            (uint8_t)report->sample.temperature_dC.source << 2);
    }

    if (Bms_CanMeasurementFresh(
            &report->sample.current_ma, now_ms,
            bms_config.current_max_age_ms)
        && (report->sample.current_ma.value >= -32768)
        && (report->sample.current_ma.value <= 32767))
    {
        current = (uint16_t)report->sample.current_ma.value;
        flags |= 0x04u;
        sources |= (uint8_t)(
            (uint8_t)report->sample.current_ma.source << 4);
    }

    /* 每个16位数均按低字节在前发送。 */
    data[0] = (uint8_t)voltage;
    data[1] = (uint8_t)(voltage >> 8);
    data[2] = (uint8_t)temperature;
    data[3] = (uint8_t)(temperature >> 8);
    data[4] = (uint8_t)current;
    data[5] = (uint8_t)(current >> 8);
    data[6] = flags;
    data[7] = sources;

    header.StdId = CAN_MEASUREMENT_STD_ID;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = 8u;

    /* HAL会在调用内把8字节复制到硬件邮箱。 */
    if (HAL_CAN_AddTxMessage(&hcan, &header, data, &mailbox) != HAL_OK)
    {
        return 0u;
    }

    can_measurement_tx_count++;
    return 1u;
}

/* 每轮主循环调用：先收已有的帧，到时间再发，不等待收发完成。 */
static void Bms_CanStatusPoll(
    const BmsTelemetry *report, uint32_t now_ms)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t tx_mailbox = 0u;
    uint8_t periodic_due;
    /* 普通模式收到的是对端发来的帧，本步只保存，不执行控制命令。 */
    if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0u)
    {
        if (HAL_CAN_GetRxMessage(
                &hcan, CAN_RX_FIFO0,
                &can_status_rx_header, can_status_rx_data) != HAL_OK)
        {
            can_status_error_count++;
            return;
        }

        can_status_rx_count++;

        Bms_HandleCanCommand(&can_status_rx_header, can_status_rx_data);
    }

    periodic_due = (uint8_t)(
        (uint32_t)(now_ms - can_status_last_ms) >= CAN_STATUS_PERIOD_MS);

    if ((periodic_due == 0u) && (can_report_requested == 0u))
    {
        return;
    }

    /* 查询不移动原来的周期基准。 */
    if (periodic_due != 0u)
    {
        can_status_last_ms = now_ms;
    }

    /* 邮箱没有空位就跳过这一帧，让主循环继续执行保护。 */
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) < 2u)
    {
        can_status_error_count++;
        return;
    }

    /* 把最新控制结果装进8字节；故障位图按低字节在前存放。 */
    can_status_tx_data[0] = (uint8_t)report->controller.machine.state;
    can_status_tx_data[1] = report->controller.machine.load_enabled;
    can_status_tx_data[2] = report->controller.clear_allowed;
    can_status_tx_data[3] = can_status_next_sequence;
    can_status_tx_data[4] = (uint8_t)report->controller.active_faults;
    can_status_tx_data[5] = (uint8_t)(report->controller.active_faults >> 8);
    can_status_tx_data[6] = (uint8_t)(report->controller.active_faults >> 16);
    can_status_tx_data[7] = (uint8_t)(report->controller.active_faults >> 24);

    tx_header.StdId = CAN_STATUS_STD_ID;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = 8u;

    /* HAL成功表示提交到发送邮箱，电脑实际收到才验证外部发送。 */
    if (HAL_CAN_AddTxMessage(
            &hcan, &tx_header, can_status_tx_data, &tx_mailbox) != HAL_OK)
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

    /* 两帧都提交成功后，清除本次查询请求。 */
    can_report_requested = 0u;
}

/* 读一个16位寄存器：先收到高字节，再收到低字节。 */
static uint8_t Bms_Ina226ReadReg16(
    uint8_t address7, uint8_t reg, uint16_t *value)
{
    uint8_t data[2];

    /* HAL要求把7位设备地址左移一位，读写方向由HAL处理。 */
    if (HAL_I2C_Mem_Read(
            &hi2c1, (uint16_t)(address7 << 1),
            reg, I2C_MEMADD_SIZE_8BIT,
            data, 2u, 5u) != HAL_OK)
    {
        return 0u;
    }

    /* 寄存器编号是1字节；寄存器内容是2字节。 */
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return 1u;
}

/* 只在业务任务启动时执行一次，寻找并核对INA226身份。 */
static void Bms_Ina226Probe(void)
{
    uint8_t address7;
    uint16_t manufacturer;
    uint16_t die_id;

    bms_ina226_found = 0u;
    bms_ina226_address7 = 0u;
    bms_ina226_ack_count = 0u;
    bms_ina226_manufacturer = 0u;
    bms_ina226_die_id = 0u;

    /* INA226的合法7位地址为0x40到0x4F。 */
    for (address7 = 0x40u; address7 <= 0x4Fu; ++address7)
    {
        if (HAL_I2C_IsDeviceReady(
                &hi2c1, (uint16_t)(address7 << 1),
                1u, 5u) != HAL_OK)
        {
            continue;
        }

        bms_ina226_ack_count++;

        if ((Bms_Ina226ReadReg16(
                address7, 0xFEu, &manufacturer) == 0u)
            || (Bms_Ina226ReadReg16(
                address7, 0xFFu, &die_id) == 0u))
        {
            continue;
        }

        bms_ina226_manufacturer = manufacturer;
        bms_ina226_die_id = die_id;

        /* 保留完整Die ID供观察，用高12位核对INA226型号。 */
        if ((manufacturer == 0x5449u)
            && ((die_id & 0xFFF0u) == 0x2260u))
        {
            bms_ina226_address7 = address7;
            bms_ina226_found = 1u;
            return;
        }
    }

    /* 未找到时保留found=0，供排查；本步不停止原有BMS演示。 */
}

/* 写16位寄存器，同样先发送高字节。 */
static uint8_t Bms_Ina226WriteReg16(uint8_t reg, uint16_t value)
{
    uint8_t data[2];

    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;

    return HAL_I2C_Mem_Write(
        &hi2c1, (uint16_t)(bms_ina226_address7 << 1),
        reg, I2C_MEMADD_SIZE_8BIT,
        data, 2u, 5u) == HAL_OK;
}

/* 身份检查通过后配置一次，并读回确认写入成功。 */
static uint8_t Bms_Ina226Configure(void)
{
    uint16_t config;

    if (bms_ina226_found == 0u)
    {
        return 0u;
    }

    if ((Bms_Ina226WriteReg16(
            0x00u, INA226_CONFIG_VALUE) == 0u)
        || (Bms_Ina226ReadReg16(
            bms_ina226_address7, 0x00u, &config) == 0u))
    {
        return 0u;
    }

    return config == INA226_CONFIG_VALUE;
}

/* 返回1：新数据；0：还没新数据；-1：读取或量程异常。 */
static int32_t Bms_Ina226ReadSample(void)
{
    uint16_t status;
    uint16_t shunt;
    uint16_t bus;
    int32_t raw;
    int32_t numerator;
    int32_t denominator;

    if (bms_ina226_configured == 0u)
    {
        return -1;
    }

    /* 0x06的第3位表示转换完成；读取该寄存器会清除此标志。 */
    if (Bms_Ina226ReadReg16(
            bms_ina226_address7, 0x06u, &status) == 0u)
    {
        return -1;
    }

    if ((status & 0x0008u) == 0u)
    {
        return 0;
    }

    if ((Bms_Ina226ReadReg16(
            bms_ina226_address7, 0x01u, &shunt) == 0u)
        || (Bms_Ina226ReadReg16(
            bms_ina226_address7, 0x02u, &bus) == 0u))
    {
        return -1;
    }

    /* 两个电流满量程端点保守判异常；总线电压检查0～36V范围。 */
    if ((shunt == 0x7FFFu) || (shunt == 0x8000u) || (bus > 28800u))
    {
        return -1;
    }

    /* 分流电压是16位有符号数，显式处理负电流补码。 */
    raw = (shunt & 0x8000u) != 0u
        ? (int32_t)shunt - 65536 : (int32_t)shunt;

    /* 每码2.5微伏：电流mA = raw × 5 / (2 × 分流电阻毫欧数)。 */
    numerator = raw * 5;
    denominator = (int32_t)INA226_SHUNT_MOHM * 2;

    bms_ina226_shunt_raw = raw;
    bms_ina226_current_ma = numerator >= 0
        ? (numerator + denominator / 2) / denominator
        : -((-numerator + denominator / 2) / denominator);

    /* 总线电压每码1.25mV，取整保存为毫伏。 */
    bms_ina226_bus_mv = ((uint32_t)bus * 5u + 2u) / 4u;

    /* 只有完整取得新数据时，才刷新时间和成功计数。 */
    bms_ina226_last_ms = HAL_GetTick();
    bms_ina226_read_count++;
    return 1;
}

/* 把INA226的新读数交给BMS；失败时不伪造零电流或刷新旧时间。 */
static void Bms_UpdateCurrentMeasurement(BmsSample *sample)
{
    sample->current_ma.source = BMS_SOURCE_EXTERNAL;
    bms_ina226_result = Bms_Ina226ReadSample();

    if (bms_ina226_result == 1)
    {
        sample->current_ma.value = bms_ina226_current_ma;
        sample->current_ma.timestamp_ms = bms_ina226_last_ms;
        sample->current_ma.valid = 1u;
    }
    else if (bms_ina226_result < 0)
    {
        sample->current_ma.valid = 0u;
        bms_ina226_error_count++;
    }

    /* result为0时保留原记录，让已有的500ms超时检查发挥作用。 */
}

/* 在创建任务之前调用一次，避免接收中断先于队列启动。 */
void Bms_CreateQueues(void)
{
    bms_command_queue = xQueueCreate(8u, sizeof(BmsCommand));
    bms_telemetry_queue = xQueueCreate(1u, sizeof(BmsTelemetry));

    if ((bms_command_queue == NULL) || (bms_telemetry_queue == NULL))
    {
        Error_Handler();
    }
}

/* 队列复制整个结构体；通信任务不会直接读取正在更新的业务数据。 */
static void Bms_PublishTelemetry(void)
{
    BmsTelemetry report = {0};

    report.sample = bms_sample;
    report.controller = bms_controller;
    report.ina_result = bms_ina226_result;
    report.bus_mv = bms_ina226_bus_mv;
    report.control_count = bms_control_cycle_count;
    report.control_max_interval_ms = bms_control_max_interval_ms;

    /* 长度为1的队列允许覆盖旧报告，不等待通信任务取走。 */
    (void)xQueueOverwrite(bms_telemetry_queue, &report);
}

/* 业务任务保留采样、保护和负载控制，不执行串口发送。 */
void Bms_RunTask(void)
{
    uint32_t now_ms;

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    Bms_ControllerInit(&bms_controller);
    Bms_UpdateIndicators(&bms_controller);

    Bms_Ina226Probe();
    bms_ina226_configured = Bms_Ina226Configure();
    bms_last_ms = HAL_GetTick();

    for (;;)
    {
        now_ms = HAL_GetTick();
        bms_rtos_loop_count++;

        if ((uint32_t)(now_ms - bms_last_ms) >= BMS_PERIOD_MS)
        {
            BmsCommand command;
            uint32_t interval_ms;

            bms_last_ms = now_ms;

            if (bms_sim_sampling_enabled == 1u)
            {
                Bms_UpdateDemoSample(&bms_sample, now_ms);
                Bms_UpdateCurrentMeasurement(&bms_sample);
            }

            /* 读取可能耗时，使用采样后的时间进行保护检查。 */
            now_ms = HAL_GetTick();
            command = Bms_TakePendingCommand();

            Bms_ControllerStep(
                &bms_controller, &bms_sample, &bms_config,
                command, DEMO_SELF_TEST_PASSED, now_ms);
            Bms_UpdateIndicators(&bms_controller);

            /* 在实际执行输出后记录时间，第一轮不计算间隔。 */
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

            bms_rtos_app_stack_free_words =
                (uint32_t)uxTaskGetStackHighWaterMark(NULL);
            bms_rtos_heap_free_bytes =
                (uint32_t)xPortGetFreeHeapSize();
        }

        osDelay(1u);
    }
}

/* 通信任务只读报告副本；启动、停止仍提交给业务任务决定。 */
void Bms_RunCommTask(void)
{
    BmsTelemetry report;
    uint32_t now_ms;
    uint32_t delay_ms;

    /* 首份报告产生前阻塞等待，避免发送未初始化的控制器状态。 */
    if (xQueuePeek(bms_telemetry_queue, &report, portMAX_DELAY) != pdPASS)
    {
        Error_Handler();
    }

    Bms_CanStart();
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) != HAL_OK)
    {
        Error_Handler();
    }

    now_ms = HAL_GetTick();
    uart_last_ms = now_ms;
    can_status_last_ms = now_ms;

    for (;;)
    {
        bms_rtos_comm_loop_count++;

        if (uart_rx_restart_required != 0u)
        {
            uart_rx_restart_required = 0u;
            if ((HAL_UART_AbortReceive(&huart1) != HAL_OK)
                || (HAL_UART_Receive_IT(
                        &huart1, &uart_rx_byte, 1u) != HAL_OK))
            {
                uart_rx_restart_required = 1u;
            }
        }

        /* Peek只复制，不取走；后续循环仍可检查这份报告是否过期。 */
        (void)xQueuePeek(bms_telemetry_queue, &report, 0u);
        now_ms = HAL_GetTick();
        Bms_CanStatusPoll(&report, now_ms);

        if ((uint32_t)(now_ms - uart_last_ms) >= UART_PERIOD_MS)
        {
            int length;

            uart_last_ms = now_ms;
            length = snprintf(
                uart_tx_buffer, sizeof(uart_tx_buffer),
                "BMS state=%u, faults=0x%08lX, latched=0x%08lX, "
                "load=%u, clear=%u, cell0_mv=%ld, temp_est_dC=%ld, "
                "i_ma=%ld, iv=%u, i_src=%u, i_age=%lu, "
                "ina=%ld, bus_mv=%lu, ctrl=%lu, gap_max=%lu\r\n",
                (unsigned int)report.controller.machine.state,
                (unsigned long)report.controller.active_faults,
                (unsigned long)report.controller.machine.latched_faults,
                (unsigned int)report.controller.machine.load_enabled,
                (unsigned int)report.controller.clear_allowed,
                (long)report.sample.cell_mv[0].value,
                (long)report.sample.temperature_dC.value,
                (long)report.sample.current_ma.value,
                (unsigned int)report.sample.current_ma.valid,
                (unsigned int)report.sample.current_ma.source,
                (unsigned long)(uint32_t)(
                    now_ms - report.sample.current_ma.timestamp_ms),
                (long)report.ina_result,
                (unsigned long)report.bus_mv,
                (unsigned long)report.control_count,
                (unsigned long)report.control_max_interval_ms);

            if ((length <= 0)
                || ((size_t)length >= sizeof(uart_tx_buffer)))
            {
                Error_Handler();
            }

            /* 日志发送失败只计数，业务任务继续检查保护。 */
            if (HAL_UART_Transmit(
                    &huart1, (const uint8_t *)uart_tx_buffer,
                    (uint16_t)length, 100u) != HAL_OK)
            {
                bms_uart_tx_error_count++;
            }

            bms_rtos_comm_stack_free_words =
                (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        }

        /* 实验延迟只阻塞本任务，不暂停采样、保护和UART接收中断。 */
        delay_ms = bms_comm_test_delay_ms;
        osDelay(delay_ms == 0u ? 1u : delay_ms);
    }
}

/* 心跳任务只负责D3，不读写BMS控制器和采样数据。 */
void Bms_RunHeartbeatTask(void)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        /* 根据上次计划时间安排下一次翻转。 */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LED_PERIOD_MS));

        HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
        bms_rtos_led_count++;

        bms_rtos_led_stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_CAN_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  /* USER CODE END 2 */

  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Error_Handler();
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* 收满一个字节时，HAL会在USART1中断中调用这个函数。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }

    /* 本次伴随UART错误时，丢弃该字节，交给主循环恢复接收。 */
    if ((huart->ErrorCode != HAL_UART_ERROR_NONE)
        || (uart_rx_restart_required != 0u))
    {
        uart_rx_restart_required = 1u;
        return;
    }

    /* 中断只转换命令，不在这里执行状态机或打印日志。 */
    switch (uart_rx_byte)
    {
        case 'S':
            Bms_PostCommand(BMS_CMD_START);
            break;

        case 'T':
            Bms_PostCommand(BMS_CMD_STOP);
            break;

        case 'C':
            Bms_PostCommand(BMS_CMD_CLEAR);
            break;
        case 'D':
            /* 实验开关：只延迟通信任务，复位后默认关闭。 */
            bms_comm_test_delay_ms =
                bms_comm_test_delay_ms == 0u ? 1000u : 0u;
            break;
        case 'P':
            /* 暂停模拟采样；主循环和保护检查继续运行。 */
            bms_sim_sampling_enabled = 0u;
            break;

        case 'R':
            /* 恢复生成新采样；解除故障仍需满足条件并发送C。 */
            bms_sim_sampling_enabled = 1u;
            break;

        default:
            /* 换行和其他字符直接忽略，不覆盖已有命令。 */
            break;
    }

    /* 上一次只接收一个字节，完成后需要再次启动下一次接收。 */
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) != HAL_OK)
    {
        uart_rx_restart_required = 1u;
    }
}

/* UART检测到溢出、帧错误等情况时，提出恢复请求。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        uart_rx_restart_required = 1u;
    }
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
    /* 程序进入错误处理时，也先撤销负载使能。 */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(
        LOAD_EN_GPIO_Port, LOAD_EN_Pin, GPIO_PIN_RESET);

  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
