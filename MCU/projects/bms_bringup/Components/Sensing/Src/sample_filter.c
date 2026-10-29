#include "sample_filter.h"
#include <stddef.h>
#include <string.h>

void SampleFilter_Init(SampleFilter *filter)
{
    if (filter != NULL)
    {
        memset(filter, 0, sizeof(*filter));
    }
}

uint8_t SampleFilter_Update(
    SampleFilter *filter, uint32_t raw, uint32_t now_ms,
    uint32_t max_age_ms, uint32_t *average)
{
    if ((filter == NULL) || (average == NULL)
        || (raw > 4095u) || (max_age_ms == 0u))
    {
        return 0u;
    }

    if ((filter->count == 0u)
        || ((uint32_t)(now_ms - filter->last_ms) >= max_age_ms))
    {
        filter->sum = 0u;
        filter->index = 0u;
        filter->count = 0u;
    }

    if (filter->count < SAMPLE_FILTER_WINDOW)
    {
        filter->count++;
    }
    else
    {
        filter->sum -= filter->samples[filter->index];
    }

    filter->samples[filter->index] = raw;
    filter->sum += raw;
    filter->index++;
    if (filter->index >= SAMPLE_FILTER_WINDOW)
    {
        filter->index = 0u;
    }
    filter->last_ms = now_ms;
    *average = (filter->sum + filter->count / 2u) / filter->count;
    return 1u;
}
