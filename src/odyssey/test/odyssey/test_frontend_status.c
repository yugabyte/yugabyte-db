
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only status.h is needed; odyssey.h would pull in PostgreSQL server headers. */
#include "../../sources/status.h"

void odyssey_test_frontend_status_to_str(void)
{
	const char *str = od_frontend_status_to_str(OD_ATTACH);
	if (strcmp(str, "OD_ATTACH") != 0) {
		fprintf(stderr, "expected OD_ATTACH, got %s\n", str);
		exit(EXIT_FAILURE);
	}
}
