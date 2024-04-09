/*-------------------------------------------------------------------------
 *
 * cron_job.h
 *	  definition of the relation that holds cron jobs (cron.job).
 *
 * Copyright (c) 2016, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef CRON_JOB_H
#define CRON_JOB_H


/* ----------------
 *		cron_job definition.
 * ----------------
 */
typedef struct FormData_cron_job
{
	int64 jobId;
#ifdef CATALOG_VARLEN
	text schedule;
	text command;
	text nodeName;
	int nodePort;
	text database;
	text userName;
	bool active;
	text jobName;
#endif
} FormData_cron_job;

/* ----------------
 *      Form_cron_jobs corresponds to a pointer to a tuple with
 *      the format of cron_job relation.
 * ----------------
 */
typedef FormData_cron_job *Form_cron_job;

/* ----------------
 *      compiler constants for cron_job
 * ----------------
 */
#define Natts_cron_job 9
#define Anum_cron_job_jobid 1
#define Anum_cron_job_schedule 2
#define Anum_cron_job_command 3
#define Anum_cron_job_nodename 4
#define Anum_cron_job_nodeport 5
#define Anum_cron_job_database 6
#define Anum_cron_job_username 7
#define Anum_cron_job_active 8
#define Anum_cron_job_jobname 9

typedef struct FormData_job_run_details
{
	int64 jobId;
	int64 runId;
	int32 job_pid;
#ifdef CATALOG_VARLEN
	text database;
	text username;
	text command;
	text status;
	text return_message;
	timestamptz start_time;
	timestamptz end_time;
#endif
} FormData_job_run_details;

typedef FormData_job_run_details *Form_job_run_details;

#define Natts_job_run_details 10
#define Anum_job_run_details_jobid 1
#define Anum_job_run_details_runid 2
#define Anum_job_run_details_job_pid 3
#define Anum_job_run_details_database 4
#define Anum_job_run_details_username 5
#define Anum_job_run_details_command 6
#define Anum_job_run_details_status 7
#define Anum_job_run_details_return_message 8
#define Anum_job_run_details_start_time 9
#define Anum_job_run_details_end_time 10

#endif /* CRON_JOB_H */
