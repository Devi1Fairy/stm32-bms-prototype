#ifndef HOST_QUEUE_H
#define HOST_QUEUE_H
#include "FreeRTOS.h"
typedef struct MockQueue *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t size);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken);
BaseType_t xQueueReset(QueueHandle_t queue);
#endif
