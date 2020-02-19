/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
