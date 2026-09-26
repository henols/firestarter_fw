// Fixture input for tests/test_check_orphan_provisional.py — NOT real
// firmware source. Consumes ONLY RURP_FIXTURE_CONSUMED_PROVISIONAL via a
// real preprocessor conditional. RURP_FIXTURE_ORPHAN_PROVISIONAL is
// deliberately never referenced anywhere in this tree.
#include "fixture_provisional.h"

#if RURP_FIXTURE_CONSUMED_PROVISIONAL
int fixture_consumed_path(void) { return 1; }
#else
int fixture_consumed_path(void) { return 0; }
#endif
