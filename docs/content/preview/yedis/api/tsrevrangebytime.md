---
title: TSREVRANGEBYTIME
linkTitle: TSREVRANGEBYTIME
description: TSREVRANGEBYTIME
menu:
  preview:
    parent: api-yedis
    weight: 2460
aliases:
  - /preview/api/redis/tsrevrangebytime
  - /preview/api/yedis/tsrevrangebytime
type: docs
---

## Synopsis

**`TSREVRANGEBYTIME key low_ts high_ts [LIMIT limit]`**

This command fetches the values for the given `low_ts`, `high_ts` range in the time series that is
specified by the given `key` ordered from newest to oldest. If `LIMIT` is specified, then at most
`limit` pairs will be fetched.

- If the given `key` is associated with non-time series data, an error is raised.
- If the given `low_ts` or `high_ts` are not a valid signed 64 bit integers, an error is raised.
- If `limit` is not a valid positive 32 bit integer, an error is raised.
- `low_ts` and `high_ts` are inclusive unless they are prefixed with `(`, in that case they are
exclusive.
- Special bounds `-inf` and `+inf` are also supported to retrieve an entire range

## Return value

Returns a list of timestamp, value pairs found in the range specified by `low_ts`, `high_ts`. If
`LIMIT` is specified, at most `limit` pairs will be fetched.

## Examples

You can do this as shown below.

```sh
TSADD ts_key 1 one 2 two 3 three 4 four 5 five 6 six
```

```
"OK"
```

```sh
TSREVRANGEBYTIME ts_key 2 4
```

```
1) "4"
2) "four"
3) "3"
4) "three"
5) "2"
6) "two"
```

2 is exclusive

```sh
TSREVRANGEBYTIME ts_key (2 4
```

```
1) "4"
2) "four"
3) "3"
4) "three"
```

2 and 4 are exclusive

```sh
TSREVRANGEBYTIME ts_key (2 (4
```

```
1) "3"
2) "three"
```

```sh
TSREVRANGEBYTIME ts_key -inf 3
```

```
1) "3"
2) "three"
3) "2"
4) "two"
5) "1"
6) "one"
```

```sh
TSREVRANGEBYTIME ts_key 2 +inf
```

```
 1) "6"
 2) "six"
 3) "5"
 4) "five"
 5) "4"
 6) "four"
 7) "3"
 8) "three"
 9) "2"
10) "two"
```

```sh
TSREVRANGEBYTIME ts_key -inf 3 LIMIT 2
```

```
1) "3"
2) "three"
3) "2"
4) "two"
```

```sh
TSREVRANGEBYTIME ts_key -inf 3 LIMIT 10
```

```
1) "3"
2) "three"
3) "2"
4) "two"
5) "1"
6) "one"
```

## See also

[`tsrangebytime`](../tsrangebytime/), [`tsadd`](../tsadd/), [`tsget`](../tsget/),
[`tsrem`](../tsrem/), [`tslastn`](../tslastn/), [`tscard`](../tscard/)
