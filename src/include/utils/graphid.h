#ifndef AG_GRAPHID_H
#define AG_GRAPHID_H

#include "postgres.h"

#include "fmgr.h"
#include "utils/syscache.h"

#include "catalog/ag_namespace.h"

typedef int64 graphid;

#define DATUM_GET_GRAPHID(d) DatumGetInt64(d)
#define AG_GETARG_GRAPHID(x) DATUM_GET_GRAPHID(PG_GETARG_DATUM(x))
#define AG_RETURN_GRAPHID(x) return Int64GetDatum(x)

// OID of graphid and _graphid
#define GRAPHIDOID \
    (GetSysCacheOid2(TYPENAMENSP, CStringGetDatum("graphid"), \
                     ObjectIdGetDatum(ag_catalog_namespace_id())))
#define GRAPHIDARRAYOID \
    (GetSysCacheOid2(TYPENAMENSP, CStringGetDatum("_graphid"), \
                     ObjectIdGetDatum(ag_catalog_namespace_id())))

#endif
