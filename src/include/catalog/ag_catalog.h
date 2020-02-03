#ifndef AG_AG_CATALOG_H
#define AG_AG_CATALOG_H

#include "postgres.h"

Oid ag_relation_id(const char *name, const char *kind);

#endif
