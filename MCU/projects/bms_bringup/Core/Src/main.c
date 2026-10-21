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

/* 本步用这个模拟结果通过自检，不代表已经实现硬件自检。 */
#define DEMO_SELF_TEST_PASSED  1u
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
static char uart_tx_buffer[96];
/* 单槽命令入口：由Watch或串口中断写入，主循环取走后清空。 */
static volatile BmsCommand bms_debug_command = BMS_CMD_NONE;
/* 1：生成新模拟采样；0：保留旧快照，用来验证采样超时。 */
static volatile uint8_t bms_sim_sampling_enabled = 1u;
/* HAL把收到的一个字节写到这里；接收结束后由回调读取。 */
static uint8_t uart_rx_byte = 0u;
/* 中断只提出重启接收的请求，具体恢复放在主循环完成。 */
static volatile uint8_t uart_rx_restart_required = 0u;
/* 第一节的模拟电压，可在Keil Watch中修改，单位mV。 */
static volatile int32_t bms_sim_cell0_mv = 3800;
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

/* 保留回环结果，便于在Keil Watch中检查。 */
static volatile uint8_t can_loopback_passed = 0u;
static CAN_RxHeaderTypeDef can_loopback_rx_header = {0};
static uint8_t can_loopback_rx_data[8] = {0};
static const uint8_t can_loopback_ok_message[] = "CAN loopback OK\r\n";

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
/* 1表示最近收到的帧与我们发送的帧一致。 */
static volatile uint8_t can_status_rx_matches = 0u;
/* 下一帧要使用的序号，超过255后从0重新开始。 */
static uint8_t can_status_next_sequence = 0u;

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

/* 生成一轮新的模拟测量；本函数不读取ADC。 */
static void Bms_MakeSimulatedSample(BmsSample *sample, uint32_t now_ms)
{
    uint32_t i;

    for (i = 0u; i < BMS_CELL_COUNT; ++i)
    {
        sample->cell_mv[i].value = 3800;  /* 每节模拟为3.800V */
        sample->cell_mv[i].timestamp_ms = now_ms;
        sample->cell_mv[i].valid = 1u;
        sample->cell_mv[i].source = BMS_SOURCE_SIMULATED;
    }

    /* 其余两节保持3800mV，第一节使用调试器设定的模拟值。 */
    sample->cell_mv[0].value = bms_sim_cell0_mv;
    sample->current_ma.value = bms_sim_current_ma;
    sample->current_ma.timestamp_ms = now_ms;
    sample->current_ma.valid = 1u;
    sample->current_ma.source = BMS_SOURCE_SIMULATED;

    sample->temperature_dC.value = bms_sim_temperature_dC;
    sample->temperature_dC.timestamp_ms = now_ms;
    sample->temperature_dC.valid = 1u;
    sample->temperature_dC.source = BMS_SOURCE_SIMULATED;
}

/* 启动阶段发送并接收一帧，核对成功返回1，失败返回0。 */
static uint8_t Bms_CanLoopbackTest(void)
{
    CAN_FilterTypeDef filter = {0};
    CAN_TxHeaderTypeDef tx_header = {0};
    const uint8_t tx_data[8] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    uint32_t tx_mailbox = 0u;
    uint32_t started_ms;
    uint32_t i;

    /* 掩码字段初始为0，先允许所有标识符进入FIFO0。 */
    filter.FilterBank = 0u;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;

    if ((HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK)
        || (HAL_CAN_Start(&hcan) != HAL_OK))
    {
        return 0u;
    }

    /* 标准数据帧：11位标识符，携带8字节测试数据。 */
    tx_header.StdId = 0x123u;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = 8u;

    if (HAL_CAN_AddTxMessage(
            &hcan, &tx_header, tx_data, &tx_mailbox) != HAL_OK)
    {
        return 0u;
    }

    /* 本步只在启动阶段等待，超时后返回失败。 */
    started_ms = HAL_GetTick();
    while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) == 0u)
    {
        if ((uint32_t)(HAL_GetTick() - started_ms) >= 100u)
        {
            return 0u;
        }
    }

    if (HAL_CAN_GetRxMessage(
            &hcan, CAN_RX_FIFO0,
            &can_loopback_rx_header, can_loopback_rx_data) != HAL_OK)
    {
        return 0u;
    }

    /* 一帧测试完成后停下CAN，随后进入原来的BMS循环。 */
    if (HAL_CAN_Stop(&hcan) != HAL_OK)
    {
        return 0u;
    }

    if ((can_loopback_rx_header.IDE != CAN_ID_STD)
        || (can_loopback_rx_header.RTR != CAN_RTR_DATA)
        || (can_loopback_rx_header.StdId != 0x123u)
        || (can_loopback_rx_header.DLC != 8u))
    {
        return 0u;
    }

    for (i = 0u; i < 8u; ++i)
    {
        if (can_loopback_rx_data[i] != tx_data[i])
        {
            return 0u;
        }
    }

    return 1u;
}

/* 每轮主循环调用：先收已有的帧，到时间再发，不等待收发完成。 */
static void Bms_CanStatusPoll(uint32_t now_ms)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t tx_mailbox = 0u;
    uint32_t i;

    /* FIFO里有帧才读取；每次调用最多处理一帧。 */
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
        can_status_rx_matches = 1u;

        /* 先核对帧头，再逐字节比较本轮发出的数据。 */
        if ((can_status_rx_header.IDE != CAN_ID_STD)
            || (can_status_rx_header.RTR != CAN_RTR_DATA)
            || (can_status_rx_header.StdId != CAN_STATUS_STD_ID)
            || (can_status_rx_header.DLC != 8u))
        {
            can_status_rx_matches = 0u;
        }

        for (i = 0u; i < 8u; ++i)
        {
            if (can_status_rx_data[i] != can_status_tx_data[i])
            {
                can_status_rx_matches = 0u;
            }
        }

        if (can_status_rx_matches == 0u)
        {
            can_status_error_count++;
        }
    }

    /* 没到发送时间就返回，让主循环继续处理保护和串口。 */
    if ((uint32_t)(now_ms - can_status_last_ms) < CAN_STATUS_PERIOD_MS)
    {
        return;
    }
    can_status_last_ms = now_ms;

    /* 本轮回环只允许一帧待接收，避免覆盖用于比较的发送数据。 */
    if ((can_status_tx_count != can_status_rx_count)
        || (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0u))
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

    /* HAL把数据放入发送邮箱；收到回环帧后，由后续主循环取出。 */
    if (HAL_CAN_AddTxMessage(
            &hcan, &tx_header, can_status_tx_data, &tx_mailbox) != HAL_OK)
    {
        can_status_error_count++;
        return;
    }

    can_status_tx_count++;
    can_status_next_sequence = (uint8_t)(can_status_next_sequence + 1u);
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
    /* 此时CubeMX生成的MX_CAN_Init已经执行完毕。 */
    can_loopback_passed = Bms_CanLoopbackTest();
    if (can_loopback_passed != 1u)
    {
        Error_Handler();
    }

    if (HAL_UART_Transmit(
            &huart1,
            can_loopback_ok_message,
            (uint16_t)(sizeof(can_loopback_ok_message) - 1u),
            100u) != HAL_OK)
    {
        Error_Handler();
    }
  
  /* 只在启动时初始化一次，循环里不重复清空控制器。 */
    Bms_ControllerInit(&bms_controller);
    Bms_UpdateIndicators(&bms_controller);
    led_last_ms = HAL_GetTick();
    uart_last_ms = led_last_ms;
    bms_last_ms = led_last_ms;

    /* 启动单帧测试结束时已停止CAN，这里重新启动周期通信。 */
    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        Error_Handler();
    }
    can_status_last_ms = led_last_ms;

    /* 第一次启动单字节接收；函数立即返回，不等待电脑发送。 */
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
                Bms_MakeSimulatedSample(&bms_sample, now_ms);
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
                "load=%u, clear=%u\r\n",
                (unsigned int)bms_controller.machine.state,
                (unsigned long)bms_controller.active_faults,
                (unsigned long)bms_controller.machine.latched_faults,
                (unsigned int)bms_controller.machine.load_enabled,
                (unsigned int)bms_controller.clear_allowed);

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
            bms_debug_command = BMS_CMD_START;
            break;

        case 'T':
            bms_debug_command = BMS_CMD_STOP;
            break;

        case 'C':
            bms_debug_command = BMS_CMD_CLEAR;
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
