#ifndef VECTOR_EXTRACT__H
#define VECTOR_EXTRACT__H

#include <postgres.h>

Datum command_bson_extract_vector_base(pgbson *document, char *pathStr, bool *isNull);

#endif
