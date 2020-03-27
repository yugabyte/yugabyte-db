/*-------------------------------------------------------------------------
 *
 * sysattr.h
 *	  POSTGRES system attribute definitions.
 *
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
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
#define ObjectIdAttributeNumber					(-2)
#define MinTransactionIdAttributeNumber			(-3)
#define MinCommandIdAttributeNumber				(-4)
#define MaxTransactionIdAttributeNumber			(-5)
#define MaxCommandIdAttributeNumber				(-6)
#define TableOidAttributeNumber					(-7)
#define FirstLowInvalidHeapAttributeNumber		(-8)

#define YBTupleIdAttributeNumber				(-8)
#define YBFirstLowInvalidAttributeNumber		(-9)

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
#define YBSystemFirstLowInvalidAttributeNumber	(-103)

#endif							/* SYSATTR_H */
