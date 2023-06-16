/*
 * pg_wait_sampling.h
 *		Headers for pg_wait_sampling extension.
 *
 * Copyright (c) 2015-2016, Postgres Professional
 *
 * IDENTIFICATION
 *	  contrib/pg_wait_sampling/pg_wait_sampling.h
 */
#ifndef __PG_WAIT_SAMPLING_H__
#define __PG_WAIT_SAMPLING_H__

#include <postgres.h>

/* Check PostgreSQL version */
#if PG_VERSION_NUM < 90600
	#error "You are trying to build pg_wait_sampling with PostgreSQL version lower than 9.6.  Please, check you environment."
#endif

#include "storage/proc.h"
#include "storage/shm_mq.h"
#include "utils/timestamp.h"

#define	PG_WAIT_SAMPLING_MAGIC		0xCA94B107
#define COLLECTOR_QUEUE_SIZE		(16 * 1024)
#define HISTORY_TIME_MULTIPLIER		10
#define PGWS_QUEUE_LOCK				0
#define PGWS_COLLECTOR_LOCK			1

typedef struct
{
	uint32			pid;
	uint32			wait_event_info;
	uint64			queryId;
	uint64			count;
} ProfileItem;

typedef struct
{
	uint32			pid;
	uint32			wait_event_info;
	uint64			queryId;
	TimestampTz		ts;
} HistoryItem;

typedef struct
{
	bool			wraparound;
	Size			index;
	Size			count;
	HistoryItem	   *items;
} History;

typedef enum
{
	NO_REQUEST,
	HISTORY_REQUEST,
	PROFILE_REQUEST,
	PROFILE_RESET
} SHMRequest;

typedef struct
{
	Latch		   *latch;
	SHMRequest		request;
	int				historySize;
	int				historyPeriod;
	int				profilePeriod;
	bool			profilePid;
	bool			profileQueries;
} CollectorShmqHeader;

/* pg_wait_sampling.c */
extern CollectorShmqHeader *pgws_collector_hdr;
extern shm_mq			   *pgws_collector_mq;
extern uint64			   *pgws_proc_queryids;
extern void pgws_init_lock_tag(LOCKTAG *tag, uint32 lock);

/* collector.c */
extern void pgws_register_wait_collector(void);
extern PGDLLEXPORT void pgws_collector_main(Datum main_arg);

#endif
