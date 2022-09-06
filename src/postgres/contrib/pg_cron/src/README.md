## Terminology

The original pg_cron extension uses some terminology whose definition may not be immediately obvious
for readers of this codebase. Below is an explanation of some of the common terms used.

Job: A set of SQL statements which are executed on behalf of some user on some database, by some
node, according to some schedule.

Job Run: The execution of a job's SQL statements. The database, user, node, etc... used in the
execution follow the job's description.

Worker: A cron process which is responsible for executing and managing the job table status of job
runs which were assigned to them.

Leader: A worker which is also responsible for assigning job runs to other workers. 

Remote Job Run: A job run which the cron leader assigns to a different cron worker than itself. 
Since the job run is executed by a worker that is usually on a different node, there is no way for
the leader to directly see the progress of this job run. Thus, it relies on querying the table to
know the job run's status.

Local Job Run: A job run which the cron leader assigns to itself.

Task: Synonym for job run.

Job Table: refers to the table cron.job. Each row in this table represents the description
of a job including what SQL statements to execute, what user to execute on
behalf of, what database to use, what schedule to follow, what node to execute on.

Job Run Table: refers to the table cron.job_run_details. This table contains the description
of a job run including what node the job run was executed on, the start and end times of
the execution and the status of the job run.

## pg_cron Operation Summary

### Postgres version

To reduce work for porting pg_cron to YugabyteDB on the postgres side, most of the existing pg_cron
code and scripts will be reused. To better understand the changes made, it may help to first
understand how the pg_cron extension operated on Postgres.

After launching Postgres, a "pg_cron launcher" process (ps aux | grep pg_cron) is created. This
process goes into a permanent loop (PgCronLauncherMain) which roughly

1. Processes any relation invalidation messages
2. Reloads extension settings if needed
3. Gets the current time and current known tasks
4. Increments a run counter for each job that is currently scheduled to run
5. Starts / Monitors job runs

The purpose of invalidation messages was to tell the cron process that the job table changed.
Processing the invalidation messages triggers a callback which sets a flag which the cron process
will check. Upon seeing the flag being set, the cron process reloads the table and stores a copy of
it in a hash table. We'll call this the job hash table.

The cron process keeps track of current running tasks through another hash table which we call the
task hash table. This hash table keeps track of information regarding the latest run of each job in
the job hash table as well as how many runs for each job are queued up (see CronTask). Queueing
occurs because the period between two job runs is shorter than the time it takes for 1 job run to
complete. Sometimes this is because one scheduled a job to run too frequently (i.e. the job always
takes at least 2 minutes to complete but someone scheduled it for once a minute). Sometimes its
because of time changes (see StartPendingRuns()).

Scheduling and monitoring job runs is always based on data in the task hash table. Tasks will go
through a series of states(see task_states.h) and this state dictates what kind of actions are
taken. Job runs can be configured to be executed with a background worker or a pgconnection.

If the cron process crashes, any queued runs are forgotten and any jobs that weren't completed or
failed in the job run table are marked as failed.

### YugabyteDB version
We want to reuse the message invalidation system so we need to give cron workers invalidation
messages when the job table changes. Since we are already logging job runs in the job run table,
we can also use that table to show which worker is assigned a job run and use the message
invalidation system to tell workers to check for their newly assigned jobs. This is why we have
SQL functions that are dedicated to inserting the invalidation messages. Now, when invalidation
messages are processed, workers will get the signal to go read the job run table to check for new
jobs.

The next major change is the separation of assigning work and performing work. A leader may assign
a job run to itself or to another worker and needs to track the status of the job run regardless
of who is running the job. Hence, a job run needs to follow 2 different sets of state change flows:
one for the leader and one for the worker. Workers only run a job whenever they are told to run a
job, so workers should never have any queued runs and their tasks can follow the same state flow
used in the postgres version of this extension. For a leader, tasks can have queued runs and can
either follow the state flow that workers use when the task is local or follow a different state
flow when the task is remote. This other state flow uses new states (see CronTaskState) which
indicate that the job isn't running locally. Now the leader can use different code in ManageCronTask
to monitor tasks that are running remotely.

Finally, workers and leaders need to consider what can happen when multiple leaders are in the
cluster. The main problems are that multiple leaders can assign different duplicate runs and
run the same job within 1 physical minute of the next (e.g. leader A runs a job, leadership switches
to B, B thinks a minute has passed because B has time skew and then it runs the same job that A
ran less than a minute ago.). See #12121 for more details.

The rest of the extension code should stay the same. There are some small changes in
table schemas which are shown in pg_cron--1.4-1--1.5.sql.
