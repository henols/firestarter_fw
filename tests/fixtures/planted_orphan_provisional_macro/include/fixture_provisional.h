// Fixture input for tests/test_check_orphan_provisional.py — NOT real
// firmware source. Defines two RURP_*_PROVISIONAL macros: one orphaned
// (zero consumers, the planted defect), one consumed (the discriminating
// control, consumed by src/fixture_consumer.cpp).
#pragma once

#define RURP_FIXTURE_ORPHAN_PROVISIONAL 1
#define RURP_FIXTURE_CONSUMED_PROVISIONAL 1
