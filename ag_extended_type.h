#ifndef AG_AG_EXTENDED_TYPE_H
#define AG_AG_EXTENDED_TYPE_H

#include "postgres.h"

#include "ag_jsonbx.h"

/*
 * Function serializes the data into the buffer provided.
 * Returns false if the type is not defined. Otherwise, true.
 */
bool ag_serialize_extended_type(StringInfo buffer, JXEntry *jxentry,
                                JsonbXValue *scalarVal);

/*
 * Function deserialized the data from the buffer pointed to by base_addr.
 * NOTE: This function writes to the error log and exits for any UNKNOWN
 * JBX_HEADER type.
 */
void ag_deserialize_extended_type(char *base_addr, uint32 offset,
                                  JsonbXValue *result);

#endif
