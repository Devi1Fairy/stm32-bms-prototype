#ifndef HOST_HAL_H
#define HOST_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { void *Instance; } I2C_HandleTypeDef;
typedef struct { void *Instance; } ADC_HandleTypeDef;
typedef struct { void *Instance; } CAN_HandleTypeDef;
typedef struct { void *Instance; uint32_t ErrorCode; } UART_HandleTypeDef;
typedef struct { unsigned int unused; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t Channel, Rank, SamplingTime; } ADC_ChannelConfTypeDef;
typedef struct {
    uint32_t FilterBank, FilterMode, FilterScale, FilterFIFOAssignment, FilterActivation;
    uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow;
} CAN_FilterTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_RxHeaderTypeDef;
typedef CAN_RxHeaderTypeDef CAN_TxHeaderTypeDef;
extern GPIO_TypeDef mock_gpio_b;
extern unsigned int mock_usart1;
extern uint32_t mock_gpio_b_clock;
#define GPIOB (&mock_gpio_b)
#define USART1 (&mock_usart1)
#define GPIO_PIN_0 1u
#define GPIO_PIN_1 2u
#define GPIO_PIN_5 32u
#define GPIO_PIN_10 1024u
#define __HAL_RCC_GPIOB_CLK_ENABLE() do { mock_gpio_b_clock++; } while (0)
#define I2C_MEMADD_SIZE_8BIT 1u
#define ADC_CHANNEL_10 10u
#define ADC_CHANNEL_11 11u
#define ADC_REGULAR_RANK_1 1u
#define ADC_SAMPLETIME_239CYCLES_5 2395u
#define CAN_FILTERMODE_IDMASK 0u
#define CAN_FILTERSCALE_32BIT 1u
#define CAN_FILTER_FIFO0 0u
#define CAN_RX_FIFO0 0u
#define CAN_ID_STD 0u
#define CAN_ID_EXT 4u
#define CAN_RTR_DATA 0u
#define CAN_RTR_REMOTE 2u
#define ENABLE 1u
#define HAL_UART_ERROR_NONE 0u
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *, uint16_t, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *, ADC_ChannelConfTypeDef *);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *, uint32_t);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
void HAL_GPIO_TogglePin(GPIO_TypeDef *, uint16_t);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *, CAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *, uint32_t, CAN_RxHeaderTypeDef *, uint8_t *);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *, CAN_TxHeaderTypeDef *, uint8_t *, uint32_t *);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
#endif
