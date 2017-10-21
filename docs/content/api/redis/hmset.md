---
title: HMSET
weight: 217
---

## SYNOPSIS
<b>`HMSET key field value [field value ...]`</b><br>
This command sets the data for the given `field` with the given `value` in the hash that is specified by `key`.
<li>If the given `field` already exists in the specified hash, this command overwrites the existing value with the given `value`.</li>
<li>If the given `key` does not exist, a new hash is created for the `key`, and the given values are inserted to the associated given fields.</li>
<li>If the given `key` is associated with a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
```
$ HMSET yugahash area1 "America" area2 "Africa"
"OK"
$ HGET yugahash area1
"America"
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
