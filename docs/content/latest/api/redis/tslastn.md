---
title: TSLASTN
linkTitle: TSLASTN
description: TSLASTN
menu:
  latest:
    parent: api-redis
    weight: 2400
aliases:
  - api/redis/tslastn
---

## SYNOPSIS
<b>`TSLASTN key N`</b><br>
This command fetches the latest N entries in the time series that is specified by the given `key`.
The elements are returned in ascending order of timestamps.

<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `N` is not a positive 32 bit integer, an error is raised.</li>

## RETURN VALUE
Returns a list of timestamp, value pairs for the latest N entries in the time series.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ TSAdd ts_key 10 v1 20 v2 30 v3 40 v4 50 v5
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSLastN ts_key 2
```
```sh
1) "40"
2) "v4"
3) "50"
4) "v5"
```
```{.sh .copy .separator-dollar}
$ TSLastN ts_key 3
```
```sh
1) "30"
2) "v3"
3) "40"
4) "v4"
5) "50"
6) "v5"
```
```{.sh .copy .separator-dollar}
$ TSLastN ts_key 9999999999
```
```sh
(error) ERR tslastn: limit field 9999999999 is not within valid bounds
```
```{.sh .copy .separator-dollar}
$ TSLastN ts_key 0
```
```sh
(error) ERR tslastn: limit field 0 is not within valid bounds
```
```{.sh .copy .separator-dollar}
$ TSLastN ts_key -1
	```
```sh
(error) ERR tslastn: limit field -1 is not within valid bounds
```

## SEE ALSO
[`tsadd`](../tsadd/), [`tsget`](../tsget/), [`tsrem`](../tsrem/), 
[`tsrangebytime`](../tsrangebytime), [`tscard`](../tscard)
