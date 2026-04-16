#include "odyssey.h"

void test_odyssey_locks_has_noneq_hash()
{
	assert(ODYSSEY_CTRL_LOCK_HASH == 1337);
	assert(ODYSSEY_CTRL_LOCK_HASH != ODYSSEY_EXEC_LOCK_HASH);
}

void odyssey_test_lock(void)
{
	test_odyssey_locks_has_noneq_hash();
}
