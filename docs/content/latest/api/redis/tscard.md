---
title: TSCARD
linkTitle: TSCARD
description: TSCARD
menu:
  latest:
    parent: api-redis
    weight: 2420
aliases:
  - /latest/api/redis/tscard
  - /latest/api/yedis/tscard
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`TSCARD key`</b><br>
This command returns the number of entires in the given time series.

<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `key` is not associated with any data, 0 is returned.</li>

## Return Value
Returns the number of entries in the given time series.

## Examples
```{.sh .copy .separator-dollar}
$ TSADD ts_key 10 v1 20 v2 30 v3 40 v4 50 v5
```
```sh
OK
```
```{.sh .copy .separator-dollar}
$ TSCARD ts_key
```
```sh
(integer) 5
```
```{.sh .copy .separator-dollar}
$ TSADD ts_key1 10 v1 20 v2 30 v3
```
```sh
OK
```
```{.sh .copy .separator-dollar}
$ TSCARD ts_key1
```
```sh
(integer) 3
```
Non-existent key returns 0.
```{.sh .copy .separator-dollar}
$ TSCARD ts_key2
```
```sh
(integer) 0
```

## See Also
[`tsadd`](../tsadd/), [`tsget`](../tsget/), [`tsrem`](../tsrem/),
[`tsrangebytime`](../tsrangebytime), [`tsrevrangebytime`](../tsrevrangebytime),
[`tslastn`](../tslastn)
