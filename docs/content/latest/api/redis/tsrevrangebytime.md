---
title: TSREVRANGEBYTIME
linkTitle: TSREVRANGEBYTIME
description: TSREVRANGEBYTIME
menu:
  latest:
    parent: api-redis
    weight: 2460
aliases:
  - api/redis/tsrevrangebytime
---

## SYNOPSIS
<b>`TSREVRANGEBYTIME key low_ts high_ts [LIMIT limit]`</b><br>
This command fetches the values for the given `low_ts`, `high_ts` range in the time series that is
specified by the given `key` ordered from newest to oldest. If `LIMIT` is specified, then at most
`limit` pairs will be fetched.

<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `low_ts` or `high_ts` are not a valid signed 64 bit integers, an error is raised.</li>
<li>If `limit` is not a valid positive 32 bit integer, an error is raised.</li>
<li>`low_ts` and `high_ts` are inclusive unless they are prefixed with `(`, in that case they are
exclusive.</li>
<li>Special bounds `-inf` and `+inf` are also supported to retrieve an entire range</li>

## RETURN VALUE
Returns a list of timestamp, value pairs found in the range specified by `low_ts`, `high_ts`. If
`LIMIT` is specified, at most `limit` pairs will be fetched.

## EXAMPLES
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
```{.sh .copy .separator-dollar}
$ TSADD cpu_usage 201710311100 “50”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSADD cpu_usage 1509474505 “75”
```
```sh
“OK”
```
```{.sh .copy .separator-dollar}
$ TSREVRANGEBYTIME cpu_usage 20 40
```
```sh
1) 40
2) “90”
3) 30
4) “60”
5) 20
6) “80”
```
```{.sh .copy .separator-dollar}
# 20 is exclusive
$ TSREVRANGEBYTIME cpu_usage (20 40
```
```sh
1) 40
2) “90”
3) 30
4) “60”
```
```{.sh .copy .separator-dollar}
# 20 and 40 are exclusive
$ TSREVRANGEBYTIME cpu_usage (20 (40
```
```sh
1) 30
2) “60”
```
```{.sh .copy .separator-dollar}
$ TSREVRANGEBYTIME cpu_usage -inf 30
```
```sh
1) 30
2) “60”
3) 20
4) “80”
5) 10
6) “70”
```
```{.sh .copy .separator-dollar}
$ TSREVRANGEBYTIME cpu_usage 20 +inf
```
```sh
1) 40
2) “90”
3) 30
4) “60”
5) 20
6) “80”
```
```{.sh .copy .separator-dollar}
$ TSREVRANGEBYTIME cpu_usage 20 +inf LIMIT 2
```
```sh
1) 40
2) “90”
3) 30
4) “60”
```
```{.sh .copy .separator-dollar}
$ TSREVRANGEBYTIME cpu_usage 20 +inf LIMIT 10
```
```sh
1) 40
2) “90”
3) 30
4) “60”
5) 20
6) “80”
```

## SEE ALSO
[`tsrangebytime`](../tsrangebytime/), [`tsadd`](../tsadd/), [`tsget`](../tsget/),
[`tsrem`](../tsrem/), [`tslastn`](../tslastn/), [`tscard`](../tscard/)
