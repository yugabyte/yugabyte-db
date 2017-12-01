---
title: HSTRLEN
weight: 2190
---

## SYNOPSIS
<b>`HSTRLEN key field`</b><br>
This command seeks the length of a string value that is associated with the given `field` in a hash table that is associated with the given `key`.
<li>If the `key` or `field` does not exist, 0 is returned.</li>
<li>If the `key` is associated with a non-hash-table value, an error is raised.</li>

## RETURN VALUE
Returns the length of the specified string.

## EXAMPLES
```
$ HMSET yugahash L1 America L2 Europe
"OK"
$ HSTRLEN yugahash L1
7
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hvals`](../hvals/)
