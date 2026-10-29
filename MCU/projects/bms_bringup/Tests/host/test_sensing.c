#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "ntc.h"
#include "sample_filter.h"

int main(void)
{
    NtcModel model = {10000.0f, 10000.0f, 3950.0f, -20.0f, 100.0f, 10u};
    NtcModel invalid;
    SampleFilter filter;
    uint32_t average = 999u;
    int32_t temperature = 999;
    int32_t warm;

    assert(Ntc_EstimateTemperature(2048u, 4095u, &model, &temperature) == 1u);
    assert(temperature == 250);
    assert(Ntc_EstimateTemperature(1849u, 4095u, &model, &warm) == 1u);
    assert(warm > temperature);
    assert(Ntc_EstimateTemperature(4095u, 4095u, &model, &temperature) == 0u);
    assert(temperature == 250);
    assert(Ntc_EstimateTemperature(0u, 4095u, &model, &temperature) == 0u);
    assert(Ntc_EstimateTemperature(10u, 4095u, &model, &temperature) == 0u);
    assert(Ntc_EstimateTemperature(4085u, 4095u, &model, &temperature) == 0u);
    assert(Ntc_EstimateTemperature(1000u, 4095u, NULL, &temperature) == 0u);
    assert(Ntc_EstimateTemperature(1000u, 4095u, &model, NULL) == 0u);
    invalid = model;
    invalid.beta_k = 0.0f;
    assert(Ntc_EstimateTemperature(1000u, 4095u, &invalid, &temperature) == 0u);
    invalid.beta_k = NAN;
    assert(Ntc_EstimateTemperature(1000u, 4095u, &invalid, &temperature) == 0u);
    assert(Ntc_EstimateTemperature(3000u, 4095u, &model, &temperature) == 1u);
    assert(temperature < 100);

    SampleFilter_Init(&filter);
    /* 前四次使用已有读数；满四个后丢弃最早的一次。 */
    assert(SampleFilter_Update(&filter, 100u, 0u, 500u, &average) && average == 100u);
    assert(SampleFilter_Update(&filter, 101u, 100u, 500u, &average) && average == 101u);
    assert(SampleFilter_Update(&filter, 102u, 200u, 500u, &average) && average == 101u);
    assert(SampleFilter_Update(&filter, 103u, 300u, 500u, &average) && average == 102u);
    assert(SampleFilter_Update(&filter, 400u, 400u, 500u, &average) && average == 177u);
    /* 恰好超时就重建窗口，不能把很久以前的读数混进来。 */
    assert(SampleFilter_Update(&filter, 800u, 900u, 500u, &average) && average == 800u);
    assert(filter.count == 1u);
    assert(!SampleFilter_Update(&filter, 4096u, 1000u, 500u, &average));
    assert(average == 800u && filter.count == 1u);
    assert(!SampleFilter_Update(NULL, 0u, 0u, 500u, &average));
    assert(!SampleFilter_Update(&filter, 0u, 0u, 0u, &average));
    assert(!SampleFilter_Update(&filter, 0u, 0u, 500u, NULL));
    SampleFilter_Init(&filter);
    assert(SampleFilter_Update(&filter, 100u, UINT32_MAX - 49u, 500u, &average));
    assert(SampleFilter_Update(&filter, 200u, 50u, 500u, &average) && average == 150u);
    filter.count = 0u;
    assert(SampleFilter_Update(&filter, 300u, 150u, 500u, &average) && average == 300u);
    puts("NTC and rolling-filter boundary checks passed.");
    return 0;
}
