---
title: TSREM
linkTitle: TSREM
description: TSREM
menu:
  latest:
    parent: api-redis
    weight: 2450
aliases:
  - api/redis/tsrem
  - api/yedis/tsrem
---

## SYNOPSIS
<b>`TSREM key timestamp [timestamp ...]`</b><br>
This command removes one or more specified timestamps from the time series that is associated with the given `key`.
<li>If the `key` exists, but is not of time series type, an error is raised.</li>
<li>If the given `timestamp` is not a valid signed 64 bit integer, an error is raised.</li>
<li>If the provided timestamps don't exist, TSREM still returns "OK". As a result, TSREM just
ensures the provided timestamps no longer exist, but doesn't provide any information about whether
they existed before the command was run.</li>

## RETURN VALUE
Returns the appropriate status string.

## EXAMPLES

The timestamp can be arbitrary integers used just for sorting values in a certain order.
```{.sh .copy .separator-dollar}
$ TSADD cpu_usage 10 “70”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSADD cpu_usage 20 “80” 30 “60” 40 “90”
```
```sh
“OK”
```

We could also encode the timestamp as “yyyymmddhhmm”, since this would still produce integers that are sortable by the actual timestamp.
```{.sh .copy .separator-dollar}
$ TSADD cpu_usage 201710311100 “50”
```
```sh
“OK”
```

A more common option would be to specify the timestamp as the unix timestamp
```{.sh .copy .separator-dollar}
$ TSADD cpu_usage 1509474505 “75”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSREM cpu_usage 20 30 40
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSRANGEBYTIME cpu_usage 10 40
```
```sh
1) 10
2) “70"
```
```{.sh .copy .separator-dollar}
$ TSREM cpu_usage 1509474505
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSGET cpu_usage 1509474505
```
```sh
(nil)
```

## SEE ALSO
[`tsadd`](../tsadd/), [`tsget`](../tsget/), [`tsrangebytime`](../tsrangebytime/),
[`tsrangebytime`](../tsrangebytime/), [`tslastn`](../tslastn/), [`tscard`](../tscard/)
