---
title: TSCARD
linkTitle: TSCARD
description: TSCARD
menu:
  v1.0:
    parent: api-redis
    weight: 2420
---

## Synopsis
<b>`TSCARD key`</b><br>
This command returns the number of entires in the given time series.

<li>If the given `key` is associated with non-timeseries data, an error is raised.</li>
<li>If the given `key` is not associated with any data, 0 is returned.</li>

## Return Value
Returns the number of entries in the given time series.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ TSADD ts_key 10 v1 20 v2 30 v3 40 v4 50 v5
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ TSCARD ts_key
```
</div>
```sh
(integer) 5
```
<div class='copy separator-dollar'>
```sh
$ TSADD ts_key1 10 v1 20 v2 30 v3
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ TSCARD ts_key1
```
</div>
```sh
(integer) 3
```
Non-existent key returns 0.
<div class='copy separator-dollar'>
```sh
$ TSCARD ts_key2
```
</div>
```sh
(integer) 0
```

## See Also
[`tsadd`](../tsadd/), [`tsget`](../tsget/), [`tsrem`](../tsrem/),
[`tsrangebytime`](../tsrangebytime), [`tsrevrangebytime`](../tsrevrangebytime),
[`tslastn`](../tslastn)
