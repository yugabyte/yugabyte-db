---
title: DEL
weight: 2040
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## SYNOPSIS
<b>`DEL key [key ...]`</b><br>
This command deletes the given `key`.

<li>If the `key` does not exist, it is ignored and not counted toward the total number of removed keys.</li>

## RETURN VALUE
Returns number of keys that were removed.

## EXAMPLES
```
$ SET yuga1 "America"
"OK"
$ SET yuga2 "Africa"
"OK"
$ DEL yuga1 yuga2 not_a_key
2
```

## SEE ALSO
[`exists`](../exists/), [`flushall`](../flushall/), [`flushdb`](../flushdb/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`mget`](../mget/), [`mset`](../mset/), [`sadd`](../sadd/), [`set`](../set/)
