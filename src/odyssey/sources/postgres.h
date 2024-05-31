#ifndef ODYSSEY_POSTGRES_H
#define ODYSSEY_POSTGRES_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define int8 int8_t
#define uint8 uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define uint64 uint64_t

#define lengthof(array) (sizeof(array) / sizeof((array)[0]))
#define pg_hton32(x) htobe32(x)

#define pg_attribute_noreturn() _NORETURN

#define HIGHBIT (0x80)
#define IS_HIGHBIT_SET(ch) ((unsigned char)(ch)&HIGHBIT)

#define FRONTEND

#include <pg_config.h>
#include <string.h>

#include <common/base64.h>
#include <common/saslprep.h>

#include <common/scram-common.h>

#if PG_VERSION_NUM >= 140000
#include <common/hmac.h>
#endif

#endif /* ODYSSEY_POSTGRES_H */
