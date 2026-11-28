"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson
Permission is hereby granted under MIT license.

Drives src/rurp_vpp_regulator.c against a fake boost plant on the host.
Lower DAC code -> higher rail (inverted response).
"""

import re
import subprocess

import pytest

from ._host_build import INCLUDE, REPO_ROOT, host_cc, run_compiler

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include "rurp_vpp_regulator.h"

typedef struct {
    int mode;            /* 0 normal, 1 adc fail, 2 zero, 3 dac fail, 4 saturate, 5 cancel */
    int gain_mv_per_code;  /* rail falls by this per code */
    int rail_at_code_zero; /* mV */
    uint16_t code;
    int boost;
    uint32_t t;
    int writes;
    int max_rail_seen;
} plant_t;

static int rail(plant_t *p) {
    if (!p->boost) return 0;
    int v = p->rail_at_code_zero - p->gain_mv_per_code * (int)p->code;
    if (v < 3000) v = 3000;
    if (v > p->max_rail_seen) p->max_rail_seen = v;
    return v;
}
static int adc(void *c, uint16_t *mv) {
    plant_t *p = c;
    if (p->mode == 1) return 1;
    if (p->mode == 2) { *mv = 0; return 0; }
    if (p->mode == 4) { *mv = 30000; return 0; }
    *mv = (uint16_t)rail(p);
    return 0;
}
static int dac(void *c, uint16_t code) {
    plant_t *p = c;
    p->writes++;
    if (p->mode == 3 && code != 4095) return 1;
    p->code = code;
    return 0;
}
static void boost(void *c, int on) { ((plant_t *)c)->boost = on; }
static uint32_t now(void *c) { return ((plant_t *)c)->t; }
static void delay_ms(void *c, uint16_t ms) { ((plant_t *)c)->t += ms; }
static int cancelled(void *c) { return ((plant_t *)c)->mode == 5; }

int main(int argc, char **argv) {
    (void)argc;
    plant_t p;
    memset(&p, 0, sizeof p);
    p.mode = atoi(argv[1]);
    p.gain_mv_per_code = atoi(argv[2]);
    p.rail_at_code_zero = atoi(argv[3]);
    p.code = 4095;
    rurp_vpp_regulator_ops_t ops = { &p, adc, dac, boost, now, delay_ms, cancelled };
    rurp_vpp_regulator_config_t cfg = { 0, 4095, 26000, 27000, 10, 3, 200, 100 };
    int target = atoi(argv[4]);
    rurp_vpp_result_t r = rurp_vpp_regulate(&ops, &cfg, (uint16_t)target, 100, 2000);
    printf("result=%d boost=%d code=%u rail=%d max=%d\n", (int)r, p.boost, p.code,
           (p.boost ? rail(&p) : 0), p.max_rail_seen);
    return 0;
}
"""


def run(tmp_path, mode, gain, top, target):
    src = tmp_path / "d.c"
    src.write_text("#include <stdlib.h>\n" + DRIVER)
    exe = tmp_path / "d"
    res = run_compiler([host_cc(), "-std=gnu11", "-Wall", "-Wextra", "-DRURP_HAS_VPP_DAC=0", "-I", str(INCLUDE),
                        str(src), str(REPO_ROOT / "src" / "rurp_vpp_regulator.c"), "-o", str(exe)])
    assert res.returncode == 0 and res.stderr == "", res.stderr
    out = subprocess.run([str(exe), str(mode), str(gain), str(top), str(target)],
                         capture_output=True, text=True)
    m = re.match(r"result=(\d+) boost=(\d+) code=(\d+) rail=(\d+) max=(\d+)", out.stdout)
    assert m, out.stdout + out.stderr
    return tuple(int(x) for x in m.groups())


OK, OUT_OF_RANGE, TIMEOUT, ADC_ERR, DAC_ERR, OVERV, ABORTED = 0, 2, 3, 4, 5, 6, 7


@pytest.mark.parametrize("gain", [4, 5, 6])  # +-20% plant gain / 18k tolerance
def test_converges_and_stays_below_overvoltage(tmp_path, gain):
    result, boost, code, rail, peak = run(tmp_path, 0, gain, 28000, 12500)
    assert result == OK and boost == 1
    assert abs(rail - 12500) <= 100
    assert peak < 27000


def test_unreachable_target_fails_closed(tmp_path):
    # Plant can only reach 6 V at DAC code 0.
    result, boost, code, rail, _ = run(tmp_path, 0, 1, 6000, 12500)
    assert result == OUT_OF_RANGE and boost == 0 and code == 4095


def test_target_above_max_rejected_without_enabling(tmp_path):
    result, boost, code, _, _ = run(tmp_path, 0, 5, 28000, 30000)
    assert result == OUT_OF_RANGE and boost == 0


@pytest.mark.parametrize("mode,expected", [(1, ADC_ERR), (2, ADC_ERR), (4, OVERV), (3, DAC_ERR), (5, ABORTED)])
def test_faults_abort_with_boost_off_and_safe_code(tmp_path, mode, expected):
    result, boost, code, _, _ = run(tmp_path, mode, 5, 28000, 12500)
    assert result == expected
    assert boost == 0
    if mode != 3:  # with a failing DAC the safe write itself cannot land
        assert code == 4095


def test_low_gain_plant_still_converges(tmp_path):
    result, _, _, rail, _ = run(tmp_path, 0, 1, 12600, 12500)
    assert result == OK and abs(rail - 12500) <= 100
