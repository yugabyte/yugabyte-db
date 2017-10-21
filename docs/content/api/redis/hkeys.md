---
title: HKEYS
weight: 214
---

## SYNOPSIS
<b>`HKEYS key`</b><br>
This command fetches all fields of the hash that is associated with the given `key`.

<li>If the `key` does not exist, an empty list is returned.</li>
<li>If the `key` is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns list of fields in the specified hash.

## EXAMPLES
```
$ HSET yugahash area1 "Africa"
1
$ HSET yugahash area2 "America"
1
$ HKEYS yugahash
1) "area1"
2) "area2"
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
