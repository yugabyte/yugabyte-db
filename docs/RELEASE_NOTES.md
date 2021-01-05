# Release Notes

Below is the complete list of release notes for every version of pg_stat_monitor.

## REL0_7_0_STABLE
### Improvements

[PG-153](https://jira.percona.com/browse/PG-153): Capture and record the application_name executing the query.

[PG-145](https://jira.percona.com/browse/PG-143): Added a new View/Query to show the actual Database name and Username.

[PG-110](https://jira.percona.com/browse/PG-110); Aggregate the number of warnings.

[PG-109](https://jira.percona.com/browse/PG-109): Added a feature to log failed queries or queries with warning messages.

[PG-150](https://jira.percona.com/browse/PG-150): Differentiate different types of queries such as SELECT, UPDATE, INSERT or DELETE.

### Bugs Fixed

[PG-111](https://jira.percona.com/browse/PG-111) Problem showing information in case of incomplete buckets. The extension was not showing information till the bucket is complete or its time is over. It is important to show the information if the bucket is completed or not.

[PG-112](https://jira.percona.com/browse/PG-112) Rename the column name from “IP” to client_ip, which is more meaningful.

[PG-148](https://jira.percona.com/browse/PG-148) Loss of query statistics/monitoring due to not enough “slots” available.

## v0.6.0
Initial Release.


## Master

### Improvements

[PG-156](https://jira.percona.com/browse/PG-156): Adding a placeholder replacement function for the prepared statement

### Bugs Fixed
