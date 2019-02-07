---
title: SETRANGE
linkTitle: SETRANGE
description: SETRANGE
menu:
  v1.0:
    parent: api-redis
    weight: 2280
---
## Synopsis
<b>`SETRANGE key offset value`</b><br>
This command overwrites the string that is associated with the given `key` with the given `value`, starting from the given `offset`.
<li> The `offset` cannot exceed 536870911.</li>
<li>If the `offset` is larger than the length of the specified string, the string will be padded with zeros up to the `offset`.</li>
<li>If the `key` does not exist, its associated string is an empty string. The resulted new string is constructed with zeros up to the given `offset` and then appended with the given `value`.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## Return Value
Returns the length of the resulted string after overwriting.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SET yugakey "YugaKey"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
```sh
$ SETRANGE yugakey 4 "Byte"
```
</div>
```sh
8
```
<div class='copy separator-dollar'>
```sh
```sh
$ GET yugakey
```
</div>
```sh
"YugaByte"
```

## See Also
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`strlen`](../strlen/)
