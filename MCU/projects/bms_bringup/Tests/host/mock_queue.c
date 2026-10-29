/* 只模拟队列的复制/满/空行为；不模拟真实调度器和中断抢占。 */
#include <assert.h>
#include <string.h>
#include "queue.h"
struct MockQueue {
    uint8_t data[8][256];
    unsigned int capacity, size, head, count;
};
static struct MockQueue queues[3];
static unsigned int created;
uint32_t mock_ipsr;
uint32_t mock_primask;
uint32_t mock_isr_sends;
uint32_t mock_task_sends;
uint32_t mock_yields;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t size)
{
    struct MockQueue *queue;
    assert(created < 3u && length <= 8u && size <= 256u);
    queue = &queues[created++];
    queue->capacity = length;
    queue->size = size;
    return queue;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    assert(queue->capacity == 1u);
    memcpy(queue->data[0], item, queue->size);
    queue->count = 1u;
    return pdPASS;
}
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait)
{
    (void)wait;
    if (queue->count == 0u) return pdFALSE;
    memcpy(item, queue->data[queue->head], queue->size);
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    if (!xQueuePeek(queue, item, wait)) return pdFALSE;
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    return pdPASS;
}
static BaseType_t Send(QueueHandle_t queue, const void *item)
{
    if (queue->count == queue->capacity) return pdFALSE;
    memcpy(queue->data[(queue->head + queue->count) % queue->capacity], item, queue->size);
    queue->count++;
    return pdPASS;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    (void)wait;
    mock_task_sends++;
    return Send(queue, item);
}
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken)
{
    *woken = pdFALSE;
    mock_isr_sends++;
    return Send(queue, item);
}
BaseType_t xQueueReset(QueueHandle_t queue)
{
    queue->head = 0u;
    queue->count = 0u;
    return pdPASS;
}
