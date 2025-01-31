/*-------------------------------------------------------------------------
 *
 * pg_statistic_ext_data.h
 *	  definition of the "extended statistics data" system catalog
 *	  (pg_statistic_ext_data)
 *
 * This catalog stores the statistical data for extended statistics objects.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/pg_statistic_ext_data.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_STATISTIC_EXT_DATA_H
#define PG_STATISTIC_EXT_DATA_H

#include "catalog/genbki.h"
#include "catalog/pg_statistic_ext_data_d.h"

/* ----------------
 *		pg_statistic_ext_data definition.  cpp turns this into
 *		typedef struct FormData_pg_statistic_ext_data
 * ----------------
 */
CATALOG(pg_statistic_ext_data,3429,StatisticExtDataRelationId)
{
	Oid			stxoid BKI_LOOKUP(pg_statistic_ext);	/* statistics object
														 * this data is for */
	bool		stxdinherit;	/* true if inheritance children are included */

#ifdef CATALOG_VARLEN			/* variable-length fields start here */

	pg_ndistinct stxdndistinct; /* ndistinct coefficients (serialized) */
	pg_dependencies stxddependencies;	/* dependencies (serialized) */
	pg_mcv_list stxdmcv;		/* MCV (serialized) */
	pg_statistic stxdexpr[1];	/* stats for expressions */

#endif

} FormData_pg_statistic_ext_data;

/* ----------------
 *		Form_pg_statistic_ext_data corresponds to a pointer to a tuple with
 *		the format of pg_statistic_ext_data relation.
 * ----------------
 */
typedef FormData_pg_statistic_ext_data *Form_pg_statistic_ext_data;

DECLARE_TOAST(pg_statistic_ext_data, 3430, 3431);

DECLARE_UNIQUE_INDEX_PKEY(pg_statistic_ext_data_stxoid_inh_index, 3433, StatisticExtDataStxoidInhIndexId, on pg_statistic_ext_data using btree(stxoid oid_ops, stxdinherit bool_ops));


#endif							/* PG_STATISTIC_EXT_DATA_H */
