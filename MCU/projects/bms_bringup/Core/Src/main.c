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
/* 在Keil Watch中写入命令；处理后会自动恢复为NONE。 */
static volatile BmsCommand bms_debug_command = BMS_CMD_NONE;
/* 第一节的模拟电压，可在Keil Watch中修改，单位mV。 */
static volatile int32_t bms_sim_cell0_mv = 3800;
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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
    sample->current_ma.value = 0;
    sample->current_ma.timestamp_ms = now_ms;
    sample->current_ma.valid = 1u;
    sample->current_ma.source = BMS_SOURCE_SIMULATED;

    sample->temperature_dC.value = 250;   /* 模拟25.0℃ */
    sample->temperature_dC.timestamp_ms = now_ms;
    sample->temperature_dC.valid = 1u;
    sample->temperature_dC.source = BMS_SOURCE_SIMULATED;
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
  /* USER CODE BEGIN 2 */
    /* 只在启动时初始化一次，循环里不重复清空控制器。 */
    Bms_ControllerInit(&bms_controller);

    led_last_ms = HAL_GetTick();
    uart_last_ms = led_last_ms;
    bms_last_ms = led_last_ms;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        uint32_t now_ms = HAL_GetTick();

        /* D3继续作为心跳灯，不表示负载已经打开。 */
        if ((uint32_t)(now_ms - led_last_ms) >= LED_PERIOD_MS)
        {
            led_last_ms = now_ms;
            HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
        }

        /* 每100毫秒处理一轮模拟采样和一条调试命令。 */
        if ((uint32_t)(now_ms - bms_last_ms) >= BMS_PERIOD_MS)
        {
            BmsCommand command;

            bms_last_ms = now_ms;
            Bms_MakeSimulatedSample(&bms_sample, now_ms);

            /* 先取出命令，再清空入口，让一条命令只处理一次。 */
            command = bms_debug_command;
            bms_debug_command = BMS_CMD_NONE;

            Bms_ControllerStep(
                &bms_controller,
                &bms_sample,
                &bms_config,
                command,
                DEMO_SELF_TEST_PASSED,
                now_ms);
        }

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
