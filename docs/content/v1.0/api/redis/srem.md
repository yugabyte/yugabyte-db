---
title: SREM
linkTitle: SREM
description: SREM
menu:
  v1.0:
    parent: api-redis
    weight: 2310
---

## Synopsis
<b>`SREM key value [value ...]`</b><br>
This command removes one or more specified members from the set that is associated with the given `key`.
<li>If the `key` does not exist, the associated set is an empty set, and the return value is zero.</li>
<li>If the `key` is associated with a value that is not a set, an error is raised.</li>
<li>If a specified `value` does not exist in the given set, that `value` is ignored and not counted toward the total of removed members.</li>

## Return Value
Returns the total number of existed members that were removed from the set.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "America"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "Moon"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ SREM yuga_world "Moon"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ SREM yuga_world "Moon"
```
</div>
```sh
0
```

## See Also
[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`smembers`](../smembers/)
