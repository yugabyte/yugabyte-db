#include "postgres.h"
#include "fmgr.h"

#ifdef PG_MODULE_MAGIC_EXT

PG_MODULE_MAGIC_EXT(
  .name = "orafce",
  .version = "4.16.6"
);

#elif defined PG_MODULE_MAGIC

PG_MODULE_MAGIC;

#endif
