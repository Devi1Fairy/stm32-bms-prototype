/*
 * 电池监测项目共用的数据类型。
 * 采样模块提供这些数据，保护和通信模块按同一套格式读取。
 * 这里只定义数据的组成；声明这些类型本身不会产生采样动作。
 */

/* 防止同一个源文件多次包含本头文件后，重复定义下面的类型。 */
#ifndef BMS_TYPES_H
#define BMS_TYPES_H

/* 提供固定宽度整数，例如 int32_t 为有符号 32 位整数。 */
#include <stdint.h>

/* 当前按三节串联电池组织数据；3 后面的 u 表示无符号整数常量。 */
#define BMS_CELL_COUNT 3u

/* 给数据来源起名字，便于分辨数据是软件构造的，还是来自采样器件。 */
typedef enum
{
    BMS_SOURCE_NONE = 0,  /* 尚未取得数据。 */
    BMS_SOURCE_SIMULATED, /* 软件构造的数据。 */
    BMS_SOURCE_ADC,       /* MCU自己的ADC采样。 */
    BMS_SOURCE_EXTERNAL,  /* 外部数字采样，例如I2C连接的INA226。 */

    /* 兼容原来的名字，数值仍然是3。 */
    BMS_SOURCE_AFE = BMS_SOURCE_EXTERNAL
} BmsSampleSource;

/* 一路测量的数据包：数值、采样时间、有效性和来源放在一起。 */
typedef struct
{
    /*
     * 测量数值，具体单位由使用它的字段名决定：mV、mA 或 0.1℃。
     * 使用有符号整数，是为了支持负电流和负温度。
     */
    int32_t value;

    /*
     * 取得新采样时的毫秒计数，后续可使用 HAL_GetTick()。
     * 它是单片机启动后的时间计数，不是年月日。
     * 重复上报旧数据时保留原时间，方便后续识别采样超时。
     */
    uint32_t timestamp_ms;

    /*
     * 0：数据无效或尚未采集；1：该次数据已通过采集和有效性检查。
     * valid 为 1 时，仍需结合 timestamp_ms 检查数据是否已经过期。
     */
    uint8_t valid;

    /* 保存来源，方便日志和测试追踪；ADC 来源也可能是模拟电池的输入。 */
    BmsSampleSource source;
} BmsMeasurement;

/*
 * 一份电池组测量数据。
 * 每节电压、电流和温度都有各自的时间与有效标志，可分别判断是否正常更新。
 */
typedef struct
{
    /* 单体电压，单位 mV；下标 0 是第一节，value 为 3700 表示 3.700 V。 */
    BmsMeasurement cell_mv[BMS_CELL_COUNT];

    /* 电流，单位 mA；暂约定正数放电、负数充电，接实物后再核对方向。 */
    BmsMeasurement current_ma;

    /* 温度，单位 0.1℃；value 为 250 表示 25.0℃，-50 表示 -5.0℃。 */
    BmsMeasurement temperature_dC;
} BmsSample;

#endif /* BMS_TYPES_H */
