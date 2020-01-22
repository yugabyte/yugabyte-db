#ifndef AG_AG_FUNC_H
#define AG_AG_FUNC_H

#include "postgres.h"

bool is_oid_ag_func(Oid func_oid, const char *func_name);
Oid get_ag_func_oid(const char *func_name, const int nargs, ...);

#endif
