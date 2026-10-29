#ifndef BSP_TRANSPORT_H
#define BSP_TRANSPORT_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

typedef struct
{
    uint32_t id;
    uint8_t extended;
    uint8_t remote;
    uint8_t length;
    uint8_t data[8];
} BspCanFrame;

typedef void (*BspUartByteHandler)(uint8_t byte);

uint8_t Bsp_CanInit(void);
/* 1有帧、0无帧、-1读取失败；失败不覆盖调用者旧帧。 */
int32_t Bsp_CanReceive(BspCanFrame *frame);
uint32_t Bsp_CanFreeMailboxes(void);
uint8_t Bsp_CanSend(const BspCanFrame *frame);

uint8_t Bsp_UartRxInit(BspUartByteHandler handler);
void Bsp_UartRxPoll(void);
uint8_t Bsp_UartWrite(const uint8_t *data, uint16_t length);

/* Core中的HAL回调只向板级模块转发，具体命令由注册函数解析。 */
void Bsp_TransportOnUartRx(UART_HandleTypeDef *uart);
void Bsp_TransportOnUartError(UART_HandleTypeDef *uart);

#endif
