/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/index_am/index_am_utils.c
 *
 *
 * Utlities for alternate index access methods
 *
 *-------------------------------------------------------------------------
 */

#include "index_am/index_am_utils.h"
#include "utils/feature_counter.h"
#include "access/relscan.h"
#include "index_am/documentdb_rum.h"

#include <miscadmin.h>

/* The registry should not be exposed outside this c file to avoid unpredictable behavior */
static BsonIndexAmEntry BsonAlternateAmRegistry[5] = { 0 };
static int BsonNumAlternateAmEntries = 0;

static const char * GetRumCatalogSchema(void);
static const char * GetRumInternalSchemaV2(void);

/* Left non-static for internal use */
BsonIndexAmEntry RumIndexAmEntry = {
	.is_single_path_index_supported = true,
	.is_unique_index_supported = true,
	.is_wild_card_supported = true,
	.is_composite_index_supported = true,
	.is_text_index_supported = true,
	.is_hashed_index_supported = true,
	.is_order_by_supported = false,
	.is_backwards_scan_supported = false,
	.is_index_only_scan_supported = false,
	.can_support_parallel_scans = false,
	.get_am_oid = RumIndexAmId,
	.get_single_path_op_family_oid = BsonRumSinglePathOperatorFamily,
	.get_composite_path_op_family_oid = BsonRumCompositeIndexOperatorFamily,
	.get_text_path_op_family_oid = BsonRumTextPathOperatorFamily,
	.get_unique_path_op_family_oid = BsonRumUniquePathOperatorFamily,
	.get_hashed_path_op_family_oid = BsonRumHashPathOperatorFamily,
	.add_explain_output = NULL, /* No explain output for RUM */
	.am_name = "rum",
	.get_opclass_catalog_schema = GetRumCatalogSchema,
	.get_opclass_internal_catalog_schema = GetRumInternalSchemaV2,
	.get_multikey_status = NULL,
	.get_truncation_status = RumGetTruncationStatus,
};

/*
 * Registers an index access method in the index AM registry.
 * The registry contains all the supported index access methods.
 * If an index was created using a different access methods than
 * the one currently set as default for creating new index on bson
 * data type, then on the read path we look into the regestry to find
 * the appropriate index AM to answer the query.
 */
void
RegisterIndexAm(BsonIndexAmEntry indexAmEntry)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"Alternate index AM registration must happen during shared_preload_libraries")));
	}

	if (BsonNumAlternateAmEntries >= MAX_ALTERNATE_INDEX_AMS)
	{
		ereport(ERROR,
				(errmsg("Only %d alternate index AMs are allowed",
						MAX_ALTERNATE_INDEX_AMS)));
	}

	BsonAlternateAmRegistry[BsonNumAlternateAmEntries++] = indexAmEntry;
}


static const BsonIndexAmEntry *
GetBsonIndexAmEntryByIndexOid(Oid indexAm)
{
	if (indexAm == RumIndexAmId())
	{
		return &RumIndexAmEntry;
	}
	else
	{
		for (int i = 0; i < BsonNumAlternateAmEntries; i++)
		{
			if (BsonAlternateAmRegistry[i].get_am_oid() == indexAm)
			{
				return &BsonAlternateAmRegistry[i];
			}
		}
	}

	return NULL;
}


bool
GetIndexAmSupportsIndexOnlyScan(Oid indexAm, Oid opFamilyOid,
								GetMultikeyStatusFunc *getMultiKeyStatus,
								GetTruncationStatusFunc *getTruncationStatus)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(indexAm);
	if (amEntry == NULL)
	{
		return false;
	}

	if (getMultiKeyStatus != NULL)
	{
		*getMultiKeyStatus = amEntry->get_multikey_status;
	}

	if (getTruncationStatus != NULL)
	{
		*getTruncationStatus = amEntry->get_truncation_status;
	}

	return amEntry->is_index_only_scan_supported &&
		   opFamilyOid == amEntry->get_composite_path_op_family_oid();
}


/* Sets the Oid of the registered alternate indexAms into an input array starting at a given index */
int
SetDynamicIndexAmOidsAndGetCount(Datum *indexAmArray, int32_t indexAmArraySize)
{
	for (int i = 0; i < BsonNumAlternateAmEntries; i++)
	{
		indexAmArray[indexAmArraySize++] = BsonAlternateAmRegistry[i].get_am_oid();
	}

	return BsonNumAlternateAmEntries;
}


/*
 * Gets a registered index AM entry along with all its capabilities and utility functions
 * by the name of the index AM. We throw an error if the requested index AM is not found,
 * as by the time we call them it should already have been registered.
 *
 * Returns NULL if the index AM is in the registry but the access method is not available.
 */
const BsonIndexAmEntry *
GetBsonIndexAmByIndexAmName(const char *index_am_name)
{
	if (strcmp(index_am_name, RumIndexAmEntry.am_name) == 0)
	{
		return &RumIndexAmEntry;
	}

	for (int i = 0; i < BsonNumAlternateAmEntries; i++)
	{
		if (strcmp(BsonAlternateAmRegistry[i].am_name, index_am_name) == 0)
		{
			BsonIndexAmEntry *amEntry = &BsonAlternateAmRegistry[i];
			if (amEntry->get_am_oid() == InvalidOid)
			{
				ereport(ERROR, (errmsg(
									"Index access method %s is not available, check the alternate_index_handler_name setting",
									index_am_name)));
			}

			return &BsonAlternateAmRegistry[i];
		}
	}

	ereport(ERROR, (errmsg("The index access method %s could not be located",
						   index_am_name)));
}


/*
 * Is the Index Acess Method used for indexing bson (as opposed to indexing TEXT, Vector, Points etc)
 * as indicated by enum MongoIndexKind_Regular.
 */
bool
IsBsonRegularIndexAm(Oid indexAm)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(indexAm);
	return amEntry != NULL;
}


bool
BsonIndexAmRequiresRangeOptimization(Oid indexAm, Oid opFamilyOid)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(indexAm);
	if (amEntry == NULL)
	{
		return false;
	}

	/* If the opFamilyOid is the composite path op family, return whether the GUC wants it enabled or not. */
	if (opFamilyOid == amEntry->get_composite_path_op_family_oid())
	{
		return false;
	}

	return true;
}


void
TryExplainByIndexAm(struct IndexScanDescData *scan, struct ExplainState *es)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(
		scan->indexRelation->rd_rel->relam);

	if (amEntry == NULL || amEntry->add_explain_output == NULL)
	{
		/* No explain output for this index AM */
		return;
	}

	amEntry->add_explain_output(scan, es);
}


/*
 * Whether the opFamily of an index is a single path index
 */
bool
IsSinglePathOpFamilyOid(Oid relam, Oid opFamilyOid)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL)
	{
		return false;
	}

	return opFamilyOid == amEntry->get_single_path_op_family_oid();
}


bool
IsUniqueCheckOpFamilyOid(Oid relam, Oid opFamilyOid)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL)
	{
		return false;
	}

	return amEntry->is_unique_index_supported &&
		   opFamilyOid == amEntry->get_unique_path_op_family_oid();
}


bool
IsHashedPathOpFamilyOid(Oid relam, Oid opFamilyOid)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL)
	{
		return false;
	}

	return amEntry->is_hashed_index_supported &&
		   opFamilyOid == amEntry->get_hashed_path_op_family_oid();
}


Oid
GetTextPathOpFamilyOid(Oid relam)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL || amEntry->get_text_path_op_family_oid == NULL)
	{
		return InvalidOid;
	}

	return amEntry->get_text_path_op_family_oid();
}


bool
IsTextPathOpFamilyOid(Oid relam, Oid opFamilyOid)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL || amEntry->get_text_path_op_family_oid == NULL)
	{
		return false;
	}

	return opFamilyOid == amEntry->get_text_path_op_family_oid();
}


/*
 * Whether the index relation was created via a composite index opclass
 */
bool
IsCompositeOpClass(Relation indexRelation)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(
		indexRelation->rd_rel->relam);
	if (amEntry == NULL)
	{
		return false;
	}

	/* Non unique indexes will have 1 attribute that has the entire composite key
	 * Unique indexes will have the first attribute matching non-unique indexes, and the
	 * second attribute matching the unique constraint key.
	 * We put the composite column first just for convenience, so we can keep the order by
	 * and query paths the same between the two.
	 */
	if (IndexRelationGetNumberOfKeyAttributes(indexRelation) == 1 ||
		IndexRelationGetNumberOfKeyAttributes(indexRelation) == 2)
	{
		return indexRelation->rd_opfamily[0] ==
			   amEntry->get_composite_path_op_family_oid();
	}

	return false;
}


bool
IsCompositeOpFamilyOid(Oid relam, Oid opFamilyOid)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);

	if (amEntry == NULL)
	{
		return false;
	}

	return amEntry->get_composite_path_op_family_oid() == opFamilyOid;
}


bool
IsCompositeOpFamilyOidWithParallelSupport(Oid relam, Oid opFamilyOid)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL)
	{
		return false;
	}

	return amEntry->get_composite_path_op_family_oid() == opFamilyOid &&
		   amEntry->can_support_parallel_scans;
}


/*
 * Whether order by is supported for a opclass of an index Am.
 */
bool
IsOrderBySupportedOnOpClass(Oid indexAm, Oid columnOpFamilyAm)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(indexAm);

	if (amEntry == NULL)
	{
		return false;
	}

	return amEntry->is_order_by_supported &&
		   amEntry->get_composite_path_op_family_oid() == columnOpFamilyAm;
}


GetMultikeyStatusFunc
GetMultiKeyStatusByRelAm(Oid relam)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL)
	{
		return NULL;
	}

	return amEntry->get_multikey_status;
}


bool
GetIndexSupportsBackwardsScan(Oid relam, bool *indexCanOrder)
{
	const BsonIndexAmEntry *amEntry = GetBsonIndexAmEntryByIndexOid(relam);
	if (amEntry == NULL)
	{
		*indexCanOrder = false;
		return false;
	}

	*indexCanOrder = amEntry->is_order_by_supported;
	return amEntry->is_backwards_scan_supported;
}


static const char *
GetRumCatalogSchema(void)
{
	return ApiCatalogSchemaName;
}


static const char *
GetRumInternalSchemaV2(void)
{
	return ApiInternalSchemaNameV2;
}
