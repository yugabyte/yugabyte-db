#include "ag_extended_type.h"

/* define the type and size of the jbx_header */
#define JBX_HEADER_TYPE uint32
#define JBX_HEADER_SIZE sizeof(JBX_HEADER_TYPE)

/* values for the JBX header field to denote the stored data type */
#define JBX_HEADER_INTEGER8 0x00000000
#define JBX_HEADER_FLOAT8 0x00000001

typedef int64 integer8;

static short ag_serialize_header(StringInfo buffer, uint32 type)
{
    short padlen;
    int offset;

    padlen = padBufferToInt(buffer);
    offset = reserveFromBuffer(buffer, JBX_HEADER_SIZE);
    *((JBX_HEADER_TYPE *)(buffer->data + offset)) = type;

    return padlen;
}

/*
 * Function serializes the data into the buffer provided.
 * Returns false if the type is not defined. Otherwise, true.
 */
bool ag_serialize_extended_type(StringInfo buffer, JXEntry *jxentry,
                                JsonbXValue *scalarVal)
{
    short padlen;
    int numlen;
    int offset;

    switch (scalarVal->type)
    {
    case jbvXInteger8:
        padlen = ag_serialize_header(buffer, JBX_HEADER_INTEGER8);

        /* copy in the integer8 data */
        numlen = sizeof(integer8);
        offset = reserveFromBuffer(buffer, numlen);
        *((integer8 *)(buffer->data + offset)) = scalarVal->val.integer8;

        *jxentry = JXENTRY_ISJSONBX | (padlen + numlen + JBX_HEADER_SIZE);
        break;

    case jbvXFloat8:
        padlen = ag_serialize_header(buffer, JBX_HEADER_FLOAT8);

        /* copy in the float8 data */
        numlen = sizeof(scalarVal->val.float8);
        offset = reserveFromBuffer(buffer, numlen);
        *((float8 *)(buffer->data + offset)) = scalarVal->val.float8;

        *jxentry = JXENTRY_ISJSONBX | (padlen + numlen + JBX_HEADER_SIZE);
        break;

    default:
        return false;
    }
    return true;
}

/*
 * Function deserialized the data from the buffer pointed to by base_addr.
 * NOTE: This function writes to the error log and exits for any UNKNOWN
 * JBX_HEADER type.
 */
void ag_deserialize_extended_type(char *base_addr, uint32 offset,
                                  JsonbXValue *result)
{
    char *base = base_addr + INTALIGN(offset);
    JBX_HEADER_TYPE jbx_header = *((JBX_HEADER_TYPE *)base);

    switch (jbx_header)
    {
    case JBX_HEADER_INTEGER8:
        result->type = jbvXInteger8;
        result->val.integer8 = *((integer8 *)(base + JBX_HEADER_SIZE));
        break;

    case JBX_HEADER_FLOAT8:
        result->type = jbvXFloat8;
        result->val.float8 = *((float8 *)(base + JBX_HEADER_SIZE));
        break;

    default:
        elog(ERROR, "Invalid JBX header value.");
    }
}
