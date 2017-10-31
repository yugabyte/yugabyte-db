---
title: HGET
weight: 212
---

## SYNOPSIS
<b>`HGET key field`</b><br>
This command fetches the value for the given `field` in the hash that is specified by the given `key`.

<li>If the given `key` or `field` does not exist, nil is returned.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

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
