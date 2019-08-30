---
title: HVALS
linkTitle: HVALS
description: HVALS
menu:
  v1.0:
    parent: api-redis
    weight: 2200
---

## Synopsis
<b>`HVALS key`</b><br>
This command selects all the values in the hash that is associated with the given `key`.

<li>If the `key` does not exist, an empty list is returned.</li>
<li>if the `key` is associated with a non-hash data, an error is raised.</li>

## Return Value
Returns list of values in the specified hash.

## Examples

You can do this as shown below.

```sh
$ HMSET yugahash area1 "America" area2 "Africa"
```

```
"OK"
```

```sh
$ HVALS yugahash
```

```
1) "America"
2) "Africa"
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/)
