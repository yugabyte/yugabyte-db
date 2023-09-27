/*-------------------------------------------------------------------------
 *
 * sysattr.h
 *	  POSTGRES system attribute definitions.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/sysattr.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SYSATTR_H
#define SYSATTR_H


/*
 * Attribute numbers for the system-defined attributes
 */
#define SelfItemPointerAttributeNumber			(-1)
#define MinTransactionIdAttributeNumber			(-2)
#define MinCommandIdAttributeNumber				(-3)
#define MaxTransactionIdAttributeNumber			(-4)
#define MaxCommandIdAttributeNumber				(-5)
#define TableOidAttributeNumber					(-6)
#define FirstLowInvalidHeapAttributeNumber 		(-7)

#define YBTupleIdAttributeNumber				(-7)
#define YBFirstLowInvalidAttributeNumber		(-8)

/*
 * RowId is an auto-generated DocDB column used for tables without a
 * primary key, but is not present in the postgres table.
 *
 * It is included here to reserve the number and for use in YB postgres
 * code that requires knowledge about this column.
 */
#define YBRowIdAttributeNumber					(-100)

#define YBIdxBaseTupleIdAttributeNumber			(-101)
#define YBUniqueIdxKeySuffixAttributeNumber		(-102)
/*
 * Previously, OID is a column in Postgres structure with value (-2).
 * Temporarily use (-103) as this column is removed from Postgres.
 */
#define YBSystemFirstLowInvalidAttributeNumber	(-103)

#define ObjectIdAttributeNumber					(-104) /*YB_TODO: remove it*/
#endif							/* SYSATTR_H */
