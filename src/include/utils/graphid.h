#ifndef AG_GRAPHID_H
#define AG_GRAPHID_H

#include "postgres.h"

typedef int64 graphid;

#define DATUM_GET_GRAPHID(d) DatumGetInt64(d)
#define AG_GETARG_GRAPHID(x) DATUM_GET_GRAPHID(PG_GETARG_DATUM(x))
#define AG_RETURN_GRAPHID(x) return Int64GetDatum(x)

#endif
