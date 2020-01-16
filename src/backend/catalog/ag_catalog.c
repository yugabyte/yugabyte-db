#include "postgres.h"

#include "catalog/namespace.h"

#include "catalog/ag_catalog.h"

Oid ag_catalog_namespace_id(void)
{
    return get_namespace_oid("ag_catalog", false);
}
