// Fixture input for tests/test_check_orphan_provisional.py — NOT real
// firmware source. A genuine consumer: a real preprocessor conditional,
// not a comment mentioning the macro.
#include "fixture_provisional.h"

#if RURP_FIXTURE_CONSUMED_PROVISIONAL
int fixture_consumed_path(void) { return 1; }
#else
int fixture_consumed_path(void) { return 0; }
#endif
