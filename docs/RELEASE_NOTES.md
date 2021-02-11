# Release Notes

Below is the complete list of release notes for every version of ``pg_stat_monitor``.

## REL0_8_0_STABLE
### Improvements

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

[PG-112](https://jira.percona.com/browse/PG-112) Rename the column from “IP” to a more meaningful "client_ip".

[PG-148](https://jira.percona.com/browse/PG-148) Loss of query statistics/monitoring due to not enough “slots” available.

## v0.6.0
Initial Release.


## Master

### Improvements

[PG-156](https://jira.percona.com/browse/PG-156): Adding a placeholder replacement function for the prepared statement

