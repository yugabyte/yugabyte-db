---
title: HGET
---

## SYNOPSIS
<b>`HGET key field`</b><br>
This command is to fetch the value for the given `field` in the hash that is specified by the given `key`.

<li>If the given `key` or `field` does not exist, null is returned.</li>
<li>If the given `key` is associated a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns the value for the given `field`

## EXAMPLES
```
$ HSET yugahash area1 "America"
1
$ HGET yugahash area1
"America"
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
