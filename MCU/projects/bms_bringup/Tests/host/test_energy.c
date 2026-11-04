#include "bms_energy.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    BmsEnergyCounter c;
    uint32_t i;

    /* 50mA、5V持续600秒。 */
    Bms_EnergyInit(&c, 500u);
    Bms_EnergyUpdate(&c, 50, 5000u, 0u);

    for (i = 1u; i <= 6000u; ++i)
        Bms_EnergyUpdate(&c, 50, 5000u, i * 100u);

    assert(c.totals.discharge_mams == 30000000u);
    assert(c.totals.discharge_nj == UINT64_C(150000000000));
    assert(c.totals.covered_ms == 600000u);
    assert(c.totals.discharge_mams / 3600u == 8333u);
    assert(c.totals.discharge_nj / 3600000u == 41666u);

    /* 重复时间戳不累计。 */
    Bms_EnergyUpdate(&c, 50, 5000u, 600000u);
    assert(c.totals.covered_ms == 600000u);

    /* 断链后首份只建立起点，负电流单独累计。 */
    Bms_EnergyBreak(&c);
    Bms_EnergyUpdate(&c, -50, 5000u, 600100u);
    Bms_EnergyUpdate(&c, -50, 5000u, 600200u);
    assert(c.totals.charge_mams == 5000u);
    assert(c.totals.charge_nj == 25000000u);

    Bms_EnergyBreak(&c);
    Bms_EnergyBreak(&c);
    assert(c.totals.gaps == 2u);

    Bms_EnergyUpdate(&c, 0, 5000u, 900000u);
    Bms_EnergyUpdate(&c, 0, 5000u, 900100u);
    assert(c.totals.covered_ms == 600200u);

    /* 小电流的零碎部分必须保留。 */
    Bms_EnergyInit(&c, 500u);
    Bms_EnergyUpdate(&c, 1, 1000u, 0u);

    for (i = 1u; i <= 1000u; ++i)
        Bms_EnergyUpdate(&c, 1, 1000u, i * 100u);

    assert(c.totals.discharge_mams == 100000u);
    assert(c.totals.discharge_mams / 3600u == 27u);

    /* 500ms允许，501ms切断；时间倒退也切断。 */
    Bms_EnergyInit(&c, 500u);
    Bms_EnergyUpdate(&c, 50, 5000u, 0u);
    Bms_EnergyUpdate(&c, 50, 5000u, 500u);
    Bms_EnergyUpdate(&c, 50, 5000u, 1001u);
    assert(c.totals.covered_ms == 500u && c.totals.gaps == 1u);

    Bms_EnergyUpdate(&c, 50, 5000u, 1101u);
    assert(c.totals.covered_ms == 600u);
    Bms_EnergyUpdate(&c, 50, 5000u, 1000u);
    Bms_EnergyUpdate(&c, 50, 5000u, 1201u);
    assert(c.totals.covered_ms == 600u && c.totals.gaps == 2u);

    /* 正常32位计时回绕。 */
    Bms_EnergyInit(&c, 500u);
    Bms_EnergyUpdate(&c, 50, 5000u, UINT32_MAX - 49u);
    Bms_EnergyUpdate(&c, 50, 5000u, 50u);
    assert(c.totals.discharge_mams == 5000u);
    assert(c.totals.covered_ms == 100u);

    Bms_EnergyUpdate(&c, 50, 36001u, 150u);
    assert(!c.have_previous && c.totals.gaps == 1u);

    /* 最小负数取绝对值和累计溢出。 */
    Bms_EnergyInit(&c, 500u);
    Bms_EnergyUpdate(&c, INT32_MIN, 36000u, 0u);
    Bms_EnergyUpdate(&c, INT32_MIN, 36000u, 100u);
    assert(c.totals.charge_mams == UINT64_C(214748364800));

    Bms_EnergyInit(&c, 500u);
    Bms_EnergyUpdate(&c, 50, 5000u, 0u);
    c.totals.discharge_mams = UINT64_MAX - 1u;
    Bms_EnergyUpdate(&c, 50, 5000u, 100u);
    assert(c.totals.overflow);
    assert(c.totals.discharge_mams == UINT64_MAX - 1u);
    assert(c.totals.discharge_nj == 0u && c.totals.covered_ms == 0u);

    Bms_EnergyUpdate(&c, 50, 5000u, 200u);
    assert(c.totals.discharge_mams == UINT64_MAX - 1u);

    Bms_EnergyInit(&c, UINT32_MAX);
    Bms_EnergyUpdate(&c, INT32_MIN, 36000u, 0u);
    Bms_EnergyUpdate(&c, INT32_MIN, 36000u, 0x7FFFFFFFu);
    assert(c.totals.overflow && c.totals.charge_mams == 0u);

    Bms_EnergyInit(NULL, 0u);
    Bms_EnergyBreak(NULL);
    Bms_EnergyUpdate(NULL, 0, 0u, 0u);

    puts("Energy calculation checks passed.");
    return 0;
}