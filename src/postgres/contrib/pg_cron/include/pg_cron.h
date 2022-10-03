/*-------------------------------------------------------------------------
 *
 * pg_cron.h
 *    definition of pg_cron data types
 *
 * Copyright (c) 2010-2015, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef PG_CRON_H
#define PG_CRON_H


/* global settings */
extern char *CronTableDatabaseName;
extern const int MaxNodenameLength;
extern const char *MyNodeName;
extern bool UseBackgroundWorkers;

#endif
