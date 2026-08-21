#include "duckdb.hpp"

#include "pgduckdb/pgduckdb_types.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"

extern "C" {
#include "postgres.h"
#include "pg_config.h"
#if PG_VERSION_NUM >= 160000
#include "varatt.h"
#endif

#ifdef USE_LZ4
#include <lz4.h>
#endif

#include "access/detoast.h"
#include "access/table.h"
#include "access/tableam.h"
#include "access/toast_internals.h"
#include "common/pg_lzcompress.h"
#include "utils/expandeddatum.h"
}

#include "pgduckdb/pgduckdb_process_lock.hpp"
#include "pgduckdb/pgduckdb_detoast.hpp"

/*
 * Following functions are direct logic found in postgres code but for duckdb execution they are needed to be thread
 * safe. Functions as palloc/pfree are exchanged with duckdb_malloc/duckdb_free. Access to toast table is protected with
 * lock also for thread safe reasons. This is initial implementation but should be revisisted in future for better
 * performances.
 */

namespace pgduckdb {

struct varlena *
PglzDecompressDatum(const struct varlena *value) {
	struct varlena *result = (struct varlena *)duckdb_malloc(VARDATA_COMPRESSED_GET_EXTSIZE(value) + VARHDRSZ);

	int32 raw_size = pglz_decompress((char *)value + VARHDRSZ_COMPRESSED, VARSIZE(value) - VARHDRSZ_COMPRESSED,
	                                 VARDATA(result), VARDATA_COMPRESSED_GET_EXTSIZE(value), true);
	if (raw_size < 0) {
		throw duckdb::InvalidInputException("(PGDuckDB/PglzDecompressDatum) Compressed pglz data is corrupt");
	}

	SET_VARSIZE(result, raw_size + VARHDRSZ);

	return result;
}

struct varlena *
Lz4DecompresDatum(const struct varlena *value) {
#ifndef USE_LZ4
	(void)value; /* keep compiler quiet */
	return NULL;
#else
	struct varlena *result = (struct varlena *)duckdb_malloc(VARDATA_COMPRESSED_GET_EXTSIZE(value) + VARHDRSZ);

	int32 raw_size = LZ4_decompress_safe((char *)value + VARHDRSZ_COMPRESSED, VARDATA(result),
	                                     VARSIZE(value) - VARHDRSZ_COMPRESSED, VARDATA_COMPRESSED_GET_EXTSIZE(value));
	if (raw_size < 0) {
		throw duckdb::InvalidInputException("(PGDuckDB/Lz4DecompresDatum) Compressed lz4 data is corrupt");
	}

	SET_VARSIZE(result, raw_size + VARHDRSZ);

	return result;
#endif
}

static struct varlena *
ToastDecompressDatum(struct varlena *attr) {
	ToastCompressionId cmid = (ToastCompressionId)TOAST_COMPRESS_METHOD(attr);
	switch (cmid) {
	case TOAST_PGLZ_COMPRESSION_ID:
		return PglzDecompressDatum(attr);
	case TOAST_LZ4_COMPRESSION_ID:
		return Lz4DecompresDatum(attr);
	default:
		throw duckdb::InvalidInputException("(PGDuckDB/ToastDecompressDatum) Invalid compression method id %d",
		                                    TOAST_COMPRESS_METHOD(attr));
		return NULL; /* keep compiler quiet */
	}
}

bool
table_relation_fetch_toast_slice(const struct varatt_external &toast_pointer, int32 attrsize, struct varlena *result) {
	Relation toast_rel = try_table_open(toast_pointer.va_toastrelid, AccessShareLock);

	if (toast_rel == NULL) {
		return false;
	}

	table_relation_fetch_toast_slice(toast_rel, toast_pointer.va_valueid, attrsize, 0, attrsize, result);

	table_close(toast_rel, AccessShareLock);
	return true;
}

static struct varlena *
ToastFetchDatum(struct varlena *attr) {
	if (!VARATT_IS_EXTERNAL_ONDISK(attr)) {
		throw duckdb::InvalidInputException("(PGDuckDB/ToastFetchDatum) Shouldn't be called for non-ondisk datums");
	}

	/* Must copy to access aligned fields */
	struct varatt_external toast_pointer;
	VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);

	int32 attrsize = VARATT_EXTERNAL_GET_EXTSIZE(toast_pointer);

	struct varlena *result = (struct varlena *)duckdb_malloc(attrsize + VARHDRSZ);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored                                                                                         \
    "-Wsign-compare" // Ignore sign comparison warning that VARATT_EXTERNAL_IS_COMPRESSED generatess
	if (VARATT_EXTERNAL_IS_COMPRESSED(toast_pointer)) {
#pragma GCC diagnostic pop
		SET_VARSIZE_COMPRESSED(result, attrsize + VARHDRSZ);
	} else {
		SET_VARSIZE(result, attrsize + VARHDRSZ);
	}

	if (attrsize == 0) {
		return result;
	}

	if (!PostgresFunctionGuard(table_relation_fetch_toast_slice, toast_pointer, attrsize, result)) {
		duckdb_free(result);
		throw duckdb::InternalException("(PGDuckDB/ToastFetchDatum) Error toast relation is NULL");
	}

	return result;
}

// This function is thread-safe and does not utilize the PostgreSQL memory context.
Datum
DetoastPostgresDatum(struct varlena *attr, bool *should_free) {
	struct varlena *toasted_value = nullptr;
	*should_free = true;
	if (VARATT_IS_EXTERNAL_ONDISK(attr)) {
		toasted_value = ToastFetchDatum(attr);
		if (VARATT_IS_COMPRESSED(toasted_value)) {
			struct varlena *tmp = toasted_value;
			toasted_value = ToastDecompressDatum(tmp);
			duckdb_free(tmp);
		}
	} else if (VARATT_IS_EXTERNAL_INDIRECT(attr)) {
		struct varatt_indirect redirect;
		VARATT_EXTERNAL_GET_POINTER(redirect, attr);
		toasted_value = (struct varlena *)redirect.pointer;
		toasted_value = reinterpret_cast<struct varlena *>(DetoastPostgresDatum(attr, should_free));
		if (attr == (struct varlena *)redirect.pointer) {
			struct varlena *result;
			result = (struct varlena *)(VARSIZE_ANY(attr));
			memcpy(result, attr, VARSIZE_ANY(attr));
			toasted_value = result;
		}
	} else if (VARATT_IS_EXTERNAL_EXPANDED(attr)) {
		ExpandedObjectHeader *eoh;
		Size resultsize;
		eoh = DatumGetEOHP(PointerGetDatum(attr));
		resultsize = EOH_get_flat_size(eoh);
		toasted_value = (struct varlena *)duckdb_malloc(resultsize);
		EOH_flatten_into(eoh, (void *)toasted_value, resultsize);
	} else if (VARATT_IS_COMPRESSED(attr)) {
		toasted_value = ToastDecompressDatum(attr);
	} else if (VARATT_IS_SHORT(attr)) {
		Size data_size = VARSIZE_SHORT(attr) - VARHDRSZ_SHORT;
		Size new_size = data_size + VARHDRSZ;
		toasted_value = (struct varlena *)duckdb_malloc(new_size);
		SET_VARSIZE(toasted_value, new_size);
		memcpy(VARDATA(toasted_value), VARDATA_SHORT(attr), data_size);
	} else {
		toasted_value = attr;
		*should_free = false;
	}

	return reinterpret_cast<Datum>(toasted_value);
}

} // namespace pgduckdb
