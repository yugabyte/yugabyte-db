/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index_am/index_am_exports.h
 *
 * Common exports for index extensibility.
 *
 *-------------------------------------------------------------------------
 */

#ifndef INDEX_AM_EXPORTS_H
#define INDEX_AM_EXPORTS_H

#include <postgres.h>
#include <utils/rel.h>

struct IndexScanDescData;
struct ExplainState;

typedef void (*TryExplainIndexFunc)(struct IndexScanDescData *scan, struct
									ExplainState *es);

typedef bool (*GetMultikeyStatusFunc)(Relation indexRelation);
typedef bool (*GetTruncationStatusFunc)(Relation indexRelation);

/*
 * Data structure for an alternative index acess method for indexing bosn.
 * It contains the indexing capability and various utility function.
 */
typedef struct
{
	bool is_single_path_index_supported;
	bool is_unique_index_supported;
	bool is_wild_card_supported;
	bool is_composite_index_supported;
	bool is_text_index_supported;
	bool is_hashed_index_supported;
	bool is_order_by_supported;
	bool is_backwards_scan_supported;
	bool is_index_only_scan_supported;
	bool can_support_parallel_scans;
	Oid (*get_am_oid)(void);
	Oid (*get_single_path_op_family_oid)(void);
	Oid (*get_composite_path_op_family_oid)(void);
	Oid (*get_text_path_op_family_oid)(void);
	Oid (*get_hashed_path_op_family_oid)(void);
	Oid (*get_unique_path_op_family_oid)(void);

	/* optional func to add explain output */
	TryExplainIndexFunc add_explain_output;

	/* The am name for create indexes */
	const char *am_name;

	/* The opclass primary catalog schema name */
	const char *(*get_opclass_catalog_schema)(void);

	/* An alternate internal schema name for op classes if not the catalog schema */
	const char *(*get_opclass_internal_catalog_schema)(void);

	/* Optional function that handles getting multi-key status for an index */
	GetMultikeyStatusFunc get_multikey_status;

	/* Optional function to that returns the truncation status of an index */
	GetTruncationStatusFunc get_truncation_status;
} BsonIndexAmEntry;

/*
 * Registers an bson index access method at system start time.
 */
void RegisterIndexAm(BsonIndexAmEntry indexAmEntry);

#endif
