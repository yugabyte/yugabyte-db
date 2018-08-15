---
title: TSRANGEBYTIME
linkTitle: TSRANGEBYTIME
description: TSRANGEBYTIME
menu:
  1.1-beta:
    parent: api-redis
    weight: 2440
aliases:
  - api/redis/tsrangebytime
  - api/yedis/tsrangebytime
---

## Synopsis
<b>`TSRANGEBYTIME key low_ts high_ts`</b><br>
This command fetches the values for the given `low_ts`, `high_ts` range in the time series that is
specified by the given `key`.

<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `low_ts` or `high_ts` are not a valid signed 64 bit integers, an error is raised.</li>
<li>`low_ts` and `high_ts` are inclusive unless they are prefixed with `(`, in that case they are
exclusive.</li>
<li>Special bounds `-inf` and `+inf` are also supported to retrieve an entire range</li>

## Return Value
Returns a list of timestamp, value pairs found in the range specified by `low_ts`, `high_ts`

## Examples
```{.sh .copy .separator-dollar}
TSADD ts 1 one 2 two 3 three 4 four 5 five 6 six
```
```sh
OK
```
```{.sh .copy .separator-dollar}
TSRANGEBYTIME ts_key 2 4
```
```sh
1) "2"
2) "two"
3) "3"
4) "three"
5) "4"
6) "four"
```
2 is exclusive
```{.sh .copy .separator-dollar}
TSRANGEBYTIME ts_key (2 4
```
```sh
1) "3"
2) "three"
3) "4"
4) "four"
```
2 and 4 are exclusive
```{.sh .copy .separator-dollar}
TSRANGEBYTIME ts_key (2 (4
```
```sh
1) "3"
2) "three"
```
```{.sh .copy .separator-dollar}
TSRANGEBYTIME ts_key -inf 3
```
```sh
1) "1"
2) "one"
3) "2"
4) "two"
5) "3"
6) "three"
```
```{.sh .copy .separator-dollar}
TSRANGEBYTIME ts_key 2 +inf
```
```sh
 1) "2"
 2) "two"
 3) "3"
 4) "three"
 5) "4"
 6) "four"
 7) "5"
 8) "five"
 9) "6"
10) "six"
```

## See Also
[`tsrevrangebytime`](../tsrevrangebytime/), [`tsadd`](../tsadd/), [`tsget`](../tsget/),
[`tsrem`](../tsrem/), [`tslastn`](../tslastn/), [`tscard`](../tscard/)
