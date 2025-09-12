---
title: TSGET
linkTitle: TSGET
description: TSGET
menu:
  preview:
    parent: api-yedis
    weight: 2420
aliases:
  - /preview/api/redis/tsget
  - /preview/api/yedis/tsget
type: docs
---

## Synopsis

**`TSGET key timestamp`**

This command fetches the value for the given `timestamp` in the time series that is specified by the
given `key`.

- If the given `key` or `timestamp` does not exist, nil is returned.
- If the given `key` is associated with non-time series data, an error is raised.
- If the given `timestamp` is not a valid signed 64 bit integer, an error is raised.

## Return value

Returns the value for the given `timestamp`.

## Examples

The timestamp can be arbitrary integers used just for sorting values in a certain order.

```sh
$ TSADD cpu_usage 10 "70"
```

```
"OK"
```

```sh
$ TSADD cpu_usage 20 "80" 30 "60" 40 "90"
```

```
"OK"
```

We could also encode the timestamp as “yyyymmddhhmm”, since this would still produce integers that are sortable by the actual timestamp.

```sh
$ TSADD cpu_usage 201710311100 "50"
```

```
"OK"
```

A more common option would be to specify the timestamp as the unix timestamp.

```sh
$ TSADD cpu_usage 1509474505 "75"
```

```
"OK"
```

```sh
$ TSGET cpu_usage 10
```

```
"70"
```

```sh
$ TSGET cpu_usage 100
```

```
(nil)
```

```sh
$ TSGET cpu_usage 201710311100
```

```
"50"
```

```sh
$ TSGET cpu_usage 1509474505
```

```
"75"
```

An error is returned when the timestamp is not an int64.

```sh
$ TSGET cpu_usage xyz
```

```
(error) Request was unable to be processed from server.
```

## See also
[`tsadd`](../tsadd/), [`tsrem`](../tsrem/), [`tsrangebytime`](../tsrangebytime/),
[`tsrevrangebytime`](../tsrevrangebytime/), [`tslastn`](../tslastn/), [`tscard`](../tscard/)
