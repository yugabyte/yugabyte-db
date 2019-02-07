---
title: TSGET
linkTitle: TSGET
description: TSGET
menu:
  v1.0:
    parent: api-redis
    weight: 2420
---

## Synopsis
<b>`TSGET key timestamp`</b><br>
This command fetches the value for the given `timestamp` in the time series that is specified by the
given `key`.

<li>If the given `key` or `timestamp` does not exist, nil is returned.</li>
<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `timestamp` is not a valid signed 64 bit integer, an error is raised.</li>

## Return Value
Returns the value for the given `timestamp`

## Examples

The timestamp can be arbitrary integers used just for sorting values in a certain order.
<div class='copy separator-dollar'>
```sh
$ TSADD cpu_usage 10 "70"
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ TSADD cpu_usage 20 "80" 30 "60" 40 "90"
```
</div>
```sh
OK
```

We could also encode the timestamp as “yyyymmddhhmm”, since this would still produce integers that are sortable by the actual timestamp.
<div class='copy separator-dollar'>
```sh
$ TSADD cpu_usage 201710311100 "50"
```
</div>
```sh
OK
```

A more common option would be to specify the timestamp as the unix timestamp.
<div class='copy separator-dollar'>
```sh
$ TSADD cpu_usage 1509474505 "75"
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ TSGET cpu_usage 10
```
</div>
```sh
"70"
```
<div class='copy separator-dollar'>
```sh
$ TSGET cpu_usage 100
```
</div>
```sh
(nil)
```
<div class='copy separator-dollar'>
```sh
$ TSGET cpu_usage 201710311100
```
</div>
```sh
"50"
```
<div class='copy separator-dollar'>
```sh
$ TSGET cpu_usage 1509474505
```
</div>
```sh
"75"
```
An error is returned when the timestamp is not an int64.
<div class='copy separator-dollar'>
```sh
$ TSGET cpu_usage xyz
```
</div>
```sh
(error) Request was unable to be processed from server.
```

## See Also
[`tsadd`](../tsadd/), [`tsrem`](../tsrem/), [`tsrangebytime`](../tsrangebytime/),
[`tsrevrangebytime`](../tsrevrangebytime/), [`tslastn`](../tslastn/), [`tscard`](../tscard/)
