# Release Notes

Below is the complete list of release notes for every version of ``pg_stat_monitor``.

## 1.0.0-rc.2

### Improvements

[PG-331](https://jira.percona.com/browse/PG-331): Changed the default value for the `pg_stat_monitor.pgsm_query_max_len` parameter from 1024 to 2048 for better data presentation in PMM 

[PG-355](https://jira.percona.com/browse/PG-355): Changed the collection of `sys_time` and `user_time` metrics so that they are now presented as an accumulative value 

[PG-286](https://jira.percona.com/browse/PG-286): Improved pg_stat_monitor performance by decreasing the overhead by more than 50%.

[PG-267](https://jira.percona.com/browse/PG-267): Added test case to verify histogram feature

[PG-359](https://jira.percona.com/browse/PG-359): Documentation: updated the `pg_stat_monitor_settings` view reference. 

[PG-344](https://jira.percona.com/browse/PG-344): Documentation: Updated the extensions order and behavior with data collection for PostgreSQL 14.

[PG-358](https://jira.percona.com/browse/PG-358): Documentation: data display of `** blk **` and `** wal **` columns when both `pg_stat_monitor` and `pg_stat_statements` are loaded together.

### Bugs Fixed 

[PG-350](https://jira.percona.com/browse/PG-350): Fixed bucket time overflow

[PG-338](https://jira.percona.com/browse/PG-338): Fixed query calls count by setting the default value for `pg_stat_monitor.pgsm_track` to `top`.

[PG-291](https://jira.percona.com/browse/PG-338): Fixed calls count.

[PG-325](https://jira.percona.com/browse/PG-325): Fixed deadlock that occurred when the query length exceeded the `pgsm_query_max_len` value. 

[PG-326](https://jira.percona.com/browse/PG-326): Added validation for `pgsm_histogram_min` and `pgsm_histogram_max` ranges

[PG-329](https://jira.percona.com/browse/PG-329): Fixed creation of `pg_stat_monitor_errors` view on SQL files.

[PG-296](https://jira.percona.com/browse/PG-296): Fixed issue with the application name not displaying in the view when changed.

[PG-290](https://jira.percona.com/browse/PG-290): Fixed issue with PostgreSQL crashing after enabling debug log level and when `pg_stat_monitor` is enabled.

[PG-166](https://jira.percona.com/browse/PG-166): Fixed issue with displaying the actual system time values instead of `NULL`



## REL0_9_0_STABLE

### Improvements

[PG-186](https://jira.percona.com/browse/PG-186): Add support to monitor query execution plan

[PG-147](https://jira.percona.com/browse/PG-147): Store top query, instead of parent query.

[PG-188](https://jira.percona.com/browse/PG-188): Added a new column to monitor the query state i.e PARSING/PLANNING/ACTIVE/FINISHED.

[PG-180](https://jira.percona.com/browse/PG-180): Schema Qualified table/relations names.

Regression Test Suite.

### Bugs Fixed

[PG-189](https://jira.percona.com/browse/PG-189): Regression crash in case of PostgreSQL 11.

[PG-187](https://jira.percona.com/browse/PG-187): Compilation Error for PostgreSQL 11 and PostgreSQL 12.

[PG-186](https://jira.percona.com/browse/PG-186): Add support to monitor query execution plan.

[PG-182](https://jira.percona.com/browse/PG-182): Added a new option for the query buffer overflow.

[PG-181](https://jira.percona.com/browse/PG-181): Segmentation fault in case of track_utility is ON.

Some Code refactoring.

## REL0_8_1

[PG-147](https://jira.percona.com/browse/PG-147): Stored Procedure Support add parentid to track caller.

[PG-177](https://jira.percona.com/browse/PG-177):  Error in Histogram ranges.

## REL0_8_0_STABLE
### Improvements

Column userid (int64) was removed.
Column dbid (int64) was removed.

Column user (string) was added (replacement for userid).
Column datname (string) was added (replacement for dbid).

[PG-176](https://jira.percona.com/browse/PG-176): Extract fully qualified relations name.

[PG-175](https://jira.percona.com/browse/PG-175): Only Superuser / Privileged user can view IP address.

[PG-174](https://jira.percona.com/browse/PG-174): Code cleanup.

[PG-173](https://jira.percona.com/browse/PG-173): Added new WAL usage statistics.

[PG-172](https://jira.percona.com/browse/PG-172): Exponential histogram for time buckets.

[PG-164](https://jira.percona.com/browse/PG-164): Query timing will be four decimal places instead of two.

[PG-167](https://jira.percona.com/browse/PG-167): SQLERRCODE must be in readable format.

### Bugs Fixed

[PG-169](https://jira.percona.com/browse/PG-169): Fixing message buffer overrun and incorrect index access to fix the server crash.

[PG-168](https://jira.percona.com/browse/PG-168): "calls" and histogram parameter does not match.

[PG-166](https://jira.percona.com/browse/PG-166): Display actual system time instead of null.

[PG-165](https://jira.percona.com/browse/PG-165): Recycle expired buckets.

[PG-150](https://jira.percona.com/browse/PG-150): Error while logging CMD Type like SELECT, UPDATE, INSERT, DELETE.


## REL0_7_2

[PG-165](https://jira.percona.com/browse/PG-165): Recycle expired buckets.

[PG-164](https://jira.percona.com/browse/PG-164): Query timing will be four decimal places instead of two.

[PG-161](https://jira.percona.com/browse/PG-161): Miscellaneous small issues.

## REL0_7_1

[PG-158](https://jira.percona.com/browse/PG-158): Segmentation fault while using pgbench with clients > 1.

[PG-159](https://jira.percona.com/browse/PG-159): Bucket start time (bucket_start_time) should be aligned with bucket_time.

[PG-160](https://jira.percona.com/browse/PG-160): Integration with PGXN.



## REL0_7_0_STABLE
### Improvements

[PG-153](https://jira.percona.com/browse/PG-153): Capture and record the application_name executing the query.

[PG-145](https://jira.percona.com/browse/PG-143): Add a new View/Query to show the actual Database name and Username.

[PG-110](https://jira.percona.com/browse/PG-110); Aggregate the number of warnings.

[PG-109](https://jira.percona.com/browse/PG-109): Log failed queries or queries with warning messages.

[PG-150](https://jira.percona.com/browse/PG-150): Differentiate different types of queries such as SELECT, UPDATE, INSERT or DELETE.

### Bugs Fixed

[PG-111](https://jira.percona.com/browse/PG-111) Show information for incomplete buckets.

[PG-148](https://jira.percona.com/browse/PG-148) Loss of query statistics/monitoring due to not enough “slots” available.

## v0.6.0
Initial Release.


## Master

### Improvements

[PG-156](https://jira.percona.com/browse/PG-156): Adding a placeholder replacement function for the prepared statement

