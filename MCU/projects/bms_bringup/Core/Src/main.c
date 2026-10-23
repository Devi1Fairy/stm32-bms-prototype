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
#include "adc.h"
#include "can.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bms_types.h"
#include "bms_controller.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

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
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* 三个动作分别记录上次执行时间。 */
static uint32_t led_last_ms = 0u;
static uint32_t uart_last_ms = 0u;
static uint32_t bms_last_ms = 0u;

/* 采样快照和控制器都需要在多次循环之间保留。 */
static BmsSample bms_sample = {0};
static BmsController bms_controller;
static char uart_tx_buffer[160];

/* 单槽命令入口：由Watch或串口中断写入，主循环取走后清空。 */
static volatile BmsCommand bms_debug_command = BMS_CMD_NONE;
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

/* 模拟温度，单位0.1℃：250表示25.0℃。 */
static volatile int32_t bms_sim_temperature_dC = 250;
/* 模拟电流，单位mA：正数表示放电，负数表示充电。 */
static volatile int32_t bms_sim_current_ma = 0;
/* 仅用于软件演示，不能直接作为真实电池的保护参数。 */
static const BmsControllerConfig bms_config = {
    .uv_threshold_mv = 3000,
    .ov_threshold_mv = 4200,
    .over_temp_dC = 600,          /* 60.0℃ */
    .discharge_limit_ma = 1000,
    .charge_limit_ma = 500,
    .voltage_max_age_ms = 500u,
    .temperature_max_age_ms = 500u,
    .current_max_age_ms = 500u,
    .recovery = {
        .cell_min_mv = 3200,
        .cell_max_mv = 4100,
        .max_temperature_dC = 550,
        .discharge_limit_ma = 800,
        .charge_limit_ma = 300,
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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 取走待处理命令；避免取出和清空之间被串口中断插入。 */
static BmsCommand Bms_TakePendingCommand(void)
{
    BmsCommand command;
    uint32_t saved_primask = __get_PRIMASK();

    /* 这里只保护几个内存操作，随后马上恢复原来的中断状态。 */
    __disable_irq();
    command = bms_debug_command;
    bms_debug_command = BMS_CMD_NONE;
    __set_PRIMASK(saved_primask);

    return command;
}

/* 串口和CAN都从这里提交请求，状态机仍由主循环统一执行。 */
static void Bms_PostCommand(BmsCommand command)
{
    uint32_t saved_primask;

    if (command == BMS_CMD_NONE)
    {
        return;
    }

    saved_primask = __get_PRIMASK();
    __disable_irq();

    /* STOP还没被取走时，后来的START或CLEAR不能把它覆盖。 */
    if ((command == BMS_CMD_STOP)
        || (bms_debug_command != BMS_CMD_STOP))
    {
        bms_debug_command = command;
    }

    __set_PRIMASK(saved_primask);
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

/* 根据本轮控制结果更新指示灯；板上LED都是低电平亮。 */
static void Bms_UpdateIndicators(const BmsController *controller)
{
    /* 先准备好默认电平：两颗灯都熄灭。 */
    GPIO_PinState run_level = GPIO_PIN_SET;
    GPIO_PinState fault_level = GPIO_PIN_SET;

    if (controller->machine.load_enabled == 1u)
    {
        run_level = GPIO_PIN_RESET;
    }

    /* 故障锁定期间一直亮，包括等待恢复和等待CLEAR的阶段。 */
    if (controller->machine.state == BMS_STATE_FAULT_LOCK)
    {
        fault_level = GPIO_PIN_RESET;
    }

    HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, run_level);
    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, fault_level);
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

/* 读取一次电位器；成功返回1，失败返回0，旧读数保留。 */
static uint8_t Bms_ReadPotentiometer(void)
{
    uint32_t raw;

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return 0u;
    }

    /* 等待本次转换，等待参数为2ms，不使用无限等待。 */
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

    bms_adc_raw = raw;
    /* 加半个除数再整除，实现整数四舍五入。 */
    bms_adc_input_mv =
        (raw * ADC_REFERENCE_MV + ADC_MAX_CODE / 2u) / ADC_MAX_CODE;

    /* 0～4095映射为2800～4400mV，方便旋钮触发欠压和过压。 */
    bms_adc_cell0_mv = (int32_t)(
        DEMO_CELL0_MIN_MV
        + (raw * DEMO_CELL0_SPAN_MV + ADC_MAX_CODE / 2u) / ADC_MAX_CODE);

    return 1u;
}

/* 第一节由ADC读取电位器，其余电压、电流和温度仍由软件模拟。 */
static void Bms_UpdateDemoSample(BmsSample *sample, uint32_t now_ms)
{
    uint32_t i;

    /* 从下标1开始，只生成第2、3节的固定模拟电压。 */
    for (i = 1u; i < BMS_CELL_COUNT; ++i)
    {
        sample->cell_mv[i].value = 3800;
        sample->cell_mv[i].timestamp_ms = now_ms;
        sample->cell_mv[i].valid = 1u;
        sample->cell_mv[i].source = BMS_SOURCE_SIMULATED;
    }

    sample->current_ma.value = bms_sim_current_ma;
    sample->current_ma.timestamp_ms = now_ms;
    sample->current_ma.valid = 1u;
    sample->current_ma.source = BMS_SOURCE_SIMULATED;

    sample->temperature_dC.value = bms_sim_temperature_dC;
    sample->temperature_dC.timestamp_ms = now_ms;
    sample->temperature_dC.valid = 1u;
    sample->temperature_dC.source = BMS_SOURCE_SIMULATED;

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
static uint8_t Bms_CanSendMeasurements(uint32_t now_ms)
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
            &bms_sample.cell_mv[0], now_ms,
            bms_config.voltage_max_age_ms)
        && (bms_sample.cell_mv[0].value >= 0)
        && (bms_sample.cell_mv[0].value <= 65535))
    {
        voltage = (uint16_t)bms_sample.cell_mv[0].value;
        flags |= 0x01u;
        sources |= (uint8_t)bms_sample.cell_mv[0].source;
    }

    if (Bms_CanMeasurementFresh(
            &bms_sample.temperature_dC, now_ms,
            bms_config.temperature_max_age_ms)
        && (bms_sample.temperature_dC.value >= -32768)
        && (bms_sample.temperature_dC.value <= 32767))
    {
        /* 负数转换为16位无符号数后，按补码字节发送。 */
        temperature = (uint16_t)bms_sample.temperature_dC.value;
        flags |= 0x02u;
        sources |= (uint8_t)(
            (uint8_t)bms_sample.temperature_dC.source << 2);
    }

    if (Bms_CanMeasurementFresh(
            &bms_sample.current_ma, now_ms,
            bms_config.current_max_age_ms)
        && (bms_sample.current_ma.value >= -32768)
        && (bms_sample.current_ma.value <= 32767))
    {
        current = (uint16_t)bms_sample.current_ma.value;
        flags |= 0x04u;
        sources |= (uint8_t)(
            (uint8_t)bms_sample.current_ma.source << 4);
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
static void Bms_CanStatusPoll(uint32_t now_ms)
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
    can_status_tx_data[0] = (uint8_t)bms_controller.machine.state;
    can_status_tx_data[1] = bms_controller.machine.load_enabled;
    can_status_tx_data[2] = bms_controller.clear_allowed;
    can_status_tx_data[3] = can_status_next_sequence;
    can_status_tx_data[4] = (uint8_t)bms_controller.active_faults;
    can_status_tx_data[5] = (uint8_t)(bms_controller.active_faults >> 8);
    can_status_tx_data[6] = (uint8_t)(bms_controller.active_faults >> 16);
    can_status_tx_data[7] = (uint8_t)(bms_controller.active_faults >> 24);

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

    if (Bms_CanSendMeasurements(now_ms) == 0u)
    {
        can_status_error_count++;
        return;
    }

    /* 两帧都提交成功后，清除本次查询请求。 */
    can_report_requested = 0u;
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
  /* USER CODE BEGIN 2 */
    /* 外设已初始化，先做一次ADC内部自校准。 */
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* 初始化控制器和指示灯，再启动周期通信。 */
    Bms_ControllerInit(&bms_controller);
    Bms_UpdateIndicators(&bms_controller);
    led_last_ms = HAL_GetTick();
    uart_last_ms = led_last_ms;
    bms_last_ms = led_last_ms;

    Bms_CanStart();
    can_status_last_ms = led_last_ms;

    /* 串口继续接收S/T/C等指令；本步尚未从CAN执行控制命令。 */
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) != HAL_OK)
    {
        Error_Handler();
    }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        uint32_t now_ms = HAL_GetTick();

        /* UART异常或接收续接失败时重启接收；失败则下一轮再试。 */
        if (uart_rx_restart_required != 0u)
        {
            /* 先清请求；之后若又产生新请求，仍能保留下来。 */
            uart_rx_restart_required = 0u;

            if ((HAL_UART_AbortReceive(&huart1) != HAL_OK)
                || (HAL_UART_Receive_IT(
                        &huart1, &uart_rx_byte, 1u) != HAL_OK))
            {
                uart_rx_restart_required = 1u;
            }
        }

        /* D3继续作为心跳灯，不表示负载已经打开。 */
        if ((uint32_t)(now_ms - led_last_ms) >= LED_PERIOD_MS)
        {
            led_last_ms = now_ms;
            HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
        }

        //* 每100毫秒执行控制；暂停采样时，保护检查仍然继续。 */
        if ((uint32_t)(now_ms - bms_last_ms) >= BMS_PERIOD_MS)
        {
            BmsCommand command;

            bms_last_ms = now_ms;

            /* 只控制是否生成新采样，下面的保护检查始终执行。 */
            if (bms_sim_sampling_enabled == 1u)
            {
                Bms_UpdateDemoSample(&bms_sample, now_ms);
            }

            /* 取出并清空命令，避免与串口中断发生竞争。 */
            command = Bms_TakePendingCommand();

            Bms_ControllerStep(
                &bms_controller,
                &bms_sample,
                &bms_config,
                command,
                DEMO_SELF_TEST_PASSED,
                now_ms);

            /* 等状态机完成本轮处理后，再把最终结果显示到灯上。 */
            Bms_UpdateIndicators(&bms_controller);
        }

        /* 控制结果更新后处理CAN；函数内部决定是否到发送时间。 */
        Bms_CanStatusPoll(now_ms);

        /* 采样/控制先执行，随后发送最新状态。 */
        if ((uint32_t)(now_ms - uart_last_ms) >= UART_PERIOD_MS)
        {
            int length;

            uart_last_ms = now_ms;
            length = snprintf(
                uart_tx_buffer,
                sizeof(uart_tx_buffer),
                "BMS state=%u, faults=0x%08lX, latched=0x%08lX, "
                "load=%u, clear=%u, raw=%lu, avg=%lu\r\n",
                (unsigned int)bms_controller.machine.state,
                (unsigned long)bms_controller.active_faults,
                (unsigned long)bms_controller.machine.latched_faults,
                (unsigned int)bms_controller.machine.load_enabled,
                (unsigned int)bms_controller.clear_allowed,
                (unsigned long)bms_adc_raw,
                (unsigned long)bms_adc_filtered_raw);

            if ((length <= 0)
                || ((size_t)length >= sizeof(uart_tx_buffer)))
            {
                Error_Handler();
            }

            if (HAL_UART_Transmit(
                    &huart1,
                    (const uint8_t *)uart_tx_buffer,
                    (uint16_t)length,
                    100u) != HAL_OK)
            {
                Error_Handler();
            }
        }
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
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
