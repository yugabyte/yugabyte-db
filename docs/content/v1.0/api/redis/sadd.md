---
title: SADD
linkTitle: SADD
description: SADD
menu:
  v1.0:
    parent: api-redis
    weight: 2250
---
## Synopsis
<b>`SADD key value [value ...]`</b><br>
This command adds one or more given values to the set that is associated with the given `key`.
<li>If the `key` does not exist, a new set is created, and members are added with the given values.
<li>If the `key` is associated with a value that is not a set, an error is raised.</li>
<li>If a specified `value` already exists in the given set, that `value` is ignored and not counted toward the total of newly added members.</li>

## Return Value
Depends on the configuration parameter `emulate_redis_responses`.
<li>
If `emulate_redis_responses` is `true`, returns
the number of new members that were added by this command not including the duplicates.
</li>
<li>
If `emulate_redis_responses` is `false`, returns OK.
</li>


## Examples
<li> `emulate_redis_responses` is `true`.
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "Africa"
```
</div>
```sh
1
```
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
$ SMEMBERS yuga_world
```
</div>
```sh
1) "Africa"
2) "America"
```
</li>

<li> `emulate_redis_responses` is `false`.
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "Africa"
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "America"
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ SMEMBERS yuga_world
```
</div>
```sh
1) "Africa"
2) "America"
```
</li>

## See Also
[`scard`](../scard/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)
