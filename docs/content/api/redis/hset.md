---
title: HSET
---

## SYNOPSIS
<b>`HSET key field value`</b><br>
This command is to set the data for the given `field` of the hash that is associated with the given `key` with the given `value`. If the `field` already exists in the hash, it is overwritten.

<li>If the given `key` does not exist, an associated hash is created, and the `field` and `value` are inserted.</li>
<li>If the given `key` is not associated with a hash, an error is raised.</li>

## RETURN VALUE
Returns 1 if a new field is inserted and 0 if an existing field is updated.

## EXAMPLES
```
$ HSET yugahash area1 "America"
1
$ HSET yugahash area1 "North America"
0
$ HGET yugahash area1
"North America"
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
