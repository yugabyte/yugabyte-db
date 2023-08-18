---
title: TSCARD
linkTitle: TSCARD
description: TSCARD
menu:
  preview:
    parent: api-yedis
    weight: 2420
aliases:
  - /preview/api/redis/tscard
  - /preview/api/yedis/tscard
type: docs
---

## Synopsis

**`TSCARD key`**

This command returns the number of entires in the given time series.

- If the given `key` is associated with non-time series data, an error is raised.
- If the given `key` is not associated with any data, 0 is returned.

## Return value

Returns the number of entries in the given time series.

## Examples

```sh
$ TSADD ts_key 10 v1 20 v2 30 v3 40 v4 50 v5
```

```
"OK"
```

```sh
$ TSCARD ts_key
```

```
(integer) 5
```

```sh
$ TSADD ts_key1 10 v1 20 v2 30 v3
```

```
"OK"
```

```sh
$ TSCARD ts_key1
```

```
(integer) 3
```
Non-existent key returns 0.

```sh
$ TSCARD ts_key2
```

```
(integer) 0
```

## See also

[`tsadd`](../tsadd/), [`tsget`](../tsget/), [`tsrem`](../tsrem/),
[`tsrangebytime`](../tsrangebytime), [`tsrevrangebytime`](../tsrevrangebytime),
[`tslastn`](../tslastn)
