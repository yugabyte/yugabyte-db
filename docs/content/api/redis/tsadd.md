---
title: TSADD
weight: 2330
---

## SYNOPSIS
<b>`TSADD key timestamp value [timestamp value ...] [EXPIRE_IN TTL] [EXPIRE_AT UNIX_TIMESTAMP]`</b><br>
This command sets the data for the given `timestamp` with the given `value` in the time series that 
is specified by `key`. This is useful in storing time series like data where the `key` could be a 
metric, the `timestamp` is the time when the metric was generated and `value` is the value of the
metric at the given `timestamp`.
<li>If the given `timestamp` already exists in the specified time series, this command overwrites the existing value with the given `value`.</li>
<li>If the given `key` does not exist, a new time series is created for the `key`, and the given values are inserted to the associated given fields.</li>
<li>If the given `key` exists, but is not of time series type, an error is raised.</li>
<li>If the given `timestamp` is not a valid signed 64 bit integer, an error is raised.</li>
<li>`EXPIRE_IN TTL` sets the TTL (time-to-live) in seconds for the entries being added.</li>
<li>`EXPIRE_AT UNIX_TIMESTAMP` ensures that the entries added would expire by the given [`UNIX_TIMESTAMP`](https://en.wikipedia.org/wiki/Unix_time) (seconds since January 1, 1970).</li>

## RETURN VALUE
Returns the appropriate status string.

## EXAMPLES
```{.sh .copy .separator-dollar}
# The timestamp can be arbitrary integers used just for sorting values in a certain order.
$ TSAdd cpu_usage 10 “70”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSAdd cpu_usage 20 “80” 30 “60” 40 “90”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
# We could also encode the timestamp as “yyyymmddhhmm”, since this would still 
# produce integers that are sortable by the actual timestamp.
$ TSAdd cpu_usage 201710311100 “50”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
# A more common option would be to specify the timestamp as the unix timestamp
$ TSAdd cpu_usage 1509474505 “75”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSGet cpu_usage 10
```
```sh
“70”
```
```{.sh .copy .separator-dollar}
$ TSGet cpu_usage 201710311100
```
```sh
“50”
```
```{.sh .copy .separator-dollar}
$ TSGet cpu_usage 1509474505
```
```sh
“75”
```
```{.sh .copy .separator-dollar}
# Set a TTL of 3600 seconds (1 hour) for an entry that we add.
$ TSAdd cpu_usage 60 “70” EXPIRE_IN 3600
# Ensure that the entry we're adding would expire at the unix_timestamp 1513642307.
$ TSAdd cpu_usage 70 “80” EXPIRE_AT 1513642307
```

## SEE ALSO
[`tsrem`](../tsrem/), [`tsget`](../tsget/), [`tsrangebytime`](../tsrangebytime/)
