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
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define LED_PERIOD_MS       500u
#define UART_PERIOD_MS      200u
#define ADC_PERIOD_MS       100u

/* 暂按参考电压3300mV估算，后续用实测值修正。 */
#define ADC_REFERENCE_MV    3300u
#define ADC_FULL_SCALE      4095u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* 三个动作分别记录上次执行时间。 */
static uint32_t led_last_ms = 0u;
static uint32_t uart_last_ms = 0u;
static uint32_t adc_last_ms = 0u;

/* 超出12位范围的初值表示尚未完成采样。 */
static volatile uint32_t adc_raw = 0xFFFFFFFFu;
static volatile uint32_t adc_mv = 0u;

/* 存放准备通过串口发送的一行文本。 */
static char uart_tx_buffer[64];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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

    /* 上电后做一次ADC内部校准。 */
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* 从初始化完成后开始计算三个周期。 */
    led_last_ms = HAL_GetTick();
    uart_last_ms = led_last_ms;
    adc_last_ms = led_last_ms;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        /* 读取当前毫秒计数；读取时间不会等待。 */
        uint32_t now_ms = HAL_GetTick();

        /* 距离上次翻转已经过了500毫秒，才改变一次灯的状态。 */
        if ((uint32_t)(now_ms - led_last_ms) >= LED_PERIOD_MS)
        {
            /* 记下本次时间；未到下一周期时，主循环可以做其他事。 */
            led_last_ms = now_ms;
            HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
        }

         /* 每100毫秒启动并读取一次ADC。 */
        if ((uint32_t)(now_ms - adc_last_ms) >= ADC_PERIOD_MS)
        {
            adc_last_ms = now_ms;

            if (HAL_ADC_Start(&hadc1) != HAL_OK)
            {
                Error_Handler();
            }

            /* 等待转换完成，超时上限为10毫秒。 */
            if (HAL_ADC_PollForConversion(&hadc1, 10u) != HAL_OK)
            {
                Error_Handler();
            }

            adc_raw = HAL_ADC_GetValue(&hadc1);

            if (HAL_ADC_Stop(&hadc1) != HAL_OK)
            {
                Error_Handler();
            }

            /* 整数换算为毫伏，加半个分母用于四舍五入。 */
            adc_mv = (adc_raw * ADC_REFERENCE_MV + ADC_FULL_SCALE / 2u)
                     / ADC_FULL_SCALE;
        }

        /* 首次有效采样后，每200毫秒发送最新读数。 */
        if (((uint32_t)(now_ms - uart_last_ms) >= UART_PERIOD_MS)
            && (adc_raw <= ADC_FULL_SCALE))
        {
            uart_last_ms = now_ms;

            /* 将数字转换成文本，写入发送缓冲区。 */
            int length = snprintf(uart_tx_buffer,
                                  sizeof(uart_tx_buffer),
                                  "ADC raw=%lu, input=%lu mV\r\n",
                                  (unsigned long)adc_raw,
                                  (unsigned long)adc_mv);

            /* 检查格式化失败或缓冲区容量不足。 */
            if ((length <= 0)
                || ((size_t)length >= sizeof(uart_tx_buffer)))
            {
                Error_Handler();
            }

            /* 只发送实际文本长度，不发送末尾空字符。 */
            if (HAL_UART_Transmit(&huart1,
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
