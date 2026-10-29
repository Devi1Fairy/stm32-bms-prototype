#ifndef SAMPLE_FILTER_H
#define SAMPLE_FILTER_H

#include <stdint.h>

#define SAMPLE_FILTER_WINDOW 4u

typedef struct
{
    uint32_t samples[SAMPLE_FILTER_WINDOW];
    uint32_t sum;
    uint32_t index;
    uint32_t count;
    uint32_t last_ms;
} SampleFilter;

void SampleFilter_Init(SampleFilter *filter);
/* 处理有效的12位ADC码；长时间没有新数据时重建窗口。 */
uint8_t SampleFilter_Update(
    SampleFilter *filter, uint32_t raw, uint32_t now_ms,
    uint32_t max_age_ms, uint32_t *average);

#endif
